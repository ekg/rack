// VST3 GUI support for Windows
// This file implements the GUI API for hosting VST3 plugin UIs on Windows

#ifdef _WIN32

#include "rack_vst3.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/base/funknown.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <atomic>

using namespace Steinberg;

// Forward declaration - defined in vst3_instance.cpp
extern "C" void* rack_vst3_plugin_get_edit_controller(RackVST3Plugin* plugin);

// Window class name for our plugin host windows
static const wchar_t* const WINDOW_CLASS_NAME = L"RackVST3PluginWindow";
static bool g_window_class_registered = false;

// Forward declaration of window procedure
static LRESULT CALLBACK PluginWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Register the window class
static bool RegisterPluginWindowClass() {
    if (g_window_class_registered) {
        return true;
    }

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PluginWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = sizeof(void*);  // Store RackVST3Gui pointer
    wc.hInstance = GetModuleHandle(NULL);
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hIconSm = NULL;

    if (RegisterClassExW(&wc) == 0) {
        return false;
    }

    g_window_class_registered = true;
    return true;
}

// IPlugFrame implementation for resize callbacks
class PlugFrame : public IPlugFrame {
public:
    PlugFrame(HWND hwnd)
        : ref_count_(1)
        , hwnd_(hwnd)
        , current_width_(0)
        , current_height_(0)
    {}

    virtual ~PlugFrame() = default;

    // IUnknown
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (!obj) {
            return kInvalidArgument;
        }

        if (FUnknownPrivate::iidEqual(_iid, FUnknown::iid) ||
            FUnknownPrivate::iidEqual(_iid, IPlugFrame::iid)) {
            addRef();
            *obj = static_cast<IPlugFrame*>(this);
            return kResultOk;
        }

        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef() override {
        return ++ref_count_;
    }

    uint32 PLUGIN_API release() override {
        uint32 count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IPlugFrame
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!view || !newSize) {
            return kInvalidArgument;
        }

        int32 width = newSize->getWidth();
        int32 height = newSize->getHeight();

        if (width <= 0 || height <= 0) {
            return kInvalidArgument;
        }

        // Resize the window
        if (hwnd_) {
            // Get current window style to calculate non-client area
            DWORD style = GetWindowLong(hwnd_, GWL_STYLE);
            DWORD exStyle = GetWindowLong(hwnd_, GWL_EXSTYLE);

            // Calculate window size including borders/title
            RECT rect = {0, 0, width, height};
            AdjustWindowRectEx(&rect, style, FALSE, exStyle);

            SetWindowPos(hwnd_, NULL, 0, 0,
                        rect.right - rect.left,
                        rect.bottom - rect.top,
                        SWP_NOMOVE | SWP_NOZORDER);
        }

        current_width_ = width;
        current_height_ = height;

        // Notify the view that resize completed
        return view->onSize(newSize);
    }

    int32 getWidth() const { return current_width_; }
    int32 getHeight() const { return current_height_; }

private:
    std::atomic<uint32> ref_count_;
    HWND hwnd_;
    int32 current_width_;
    int32 current_height_;
};

// Internal GUI state
struct RackVST3Gui {
    // Plugin reference (weak - plugin must outlive GUI)
    RackVST3Plugin* plugin;

    // VST3 view
    IPtr<IPlugView> view;

    // Our plug frame implementation
    PlugFrame* frame;

    // Windows resources
    HWND hwnd;

    // State
    bool visible;
    uint32_t width;
    uint32_t height;

    // Error info
    char error_message[256];

    RackVST3Gui()
        : plugin(nullptr)
        , frame(nullptr)
        , hwnd(NULL)
        , visible(false)
        , width(0)
        , height(0)
    {
        error_message[0] = '\0';
    }

    ~RackVST3Gui() {
        // Detach view first if attached
        if (view && hwnd) {
            view->removed();
        }
        view = nullptr;

        // Release frame
        if (frame) {
            frame->release();
            frame = nullptr;
        }

        // Destroy window
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = NULL;
        }
    }
};

// Window procedure
static LRESULT CALLBACK PluginWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    RackVST3Gui* gui = reinterpret_cast<RackVST3Gui*>(GetWindowLongPtr(hwnd, 0));

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtr(hwnd, 0, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }

        case WM_SIZE: {
            if (gui && gui->view) {
                UINT width = LOWORD(lParam);
                UINT height = HIWORD(lParam);
                gui->width = width;
                gui->height = height;

                ViewRect newRect(0, 0, width, height);
                gui->view->onSize(&newRect);
            }
            return 0;
        }

        case WM_CLOSE: {
            if (gui) {
                gui->visible = false;
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;  // Don't let DefWindowProc destroy the window
        }

        case WM_DESTROY: {
            // Window is being destroyed
            return 0;
        }

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

extern "C" {

RackVST3Gui* rack_vst3_gui_create(RackVST3Plugin* plugin) {
    if (!plugin) {
        return nullptr;
    }

    // Register window class if not already done
    if (!RegisterPluginWindowClass()) {
        return nullptr;
    }

    // Get the edit controller from the plugin
    void* controller_ptr = rack_vst3_plugin_get_edit_controller(plugin);
    if (!controller_ptr) {
        return nullptr;
    }

    auto* controller = static_cast<Vst::IEditController*>(controller_ptr);

    // Create the plugin view
    IPlugView* plugView = controller->createView(Vst::ViewType::kEditor);
    if (!plugView) {
        // Plugin doesn't have a GUI
        return nullptr;
    }

    // Check if the view supports Windows HWND
    if (plugView->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
        plugView->release();
        return nullptr;
    }

    // Create GUI struct
    auto* gui = new(std::nothrow) RackVST3Gui();
    if (!gui) {
        plugView->release();
        return nullptr;
    }

    gui->plugin = plugin;
    gui->view = owned(plugView);  // Takes ownership

    // Get the view's preferred size
    ViewRect viewRect;
    if (gui->view->getSize(&viewRect) == kResultTrue) {
        gui->width = viewRect.getWidth();
        gui->height = viewRect.getHeight();
    } else {
        // Default size if getSize fails
        gui->width = 800;
        gui->height = 600;
    }

    // Ensure minimum size
    if (gui->width < 100) gui->width = 800;
    if (gui->height < 100) gui->height = 600;

    // Calculate window size including borders
    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD exStyle = 0;
    RECT rect = {0, 0, static_cast<LONG>(gui->width), static_cast<LONG>(gui->height)};
    AdjustWindowRectEx(&rect, style, FALSE, exStyle);

    // Create the window
    gui->hwnd = CreateWindowExW(
        exStyle,
        WINDOW_CLASS_NAME,
        L"VST3 Plugin",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,  // Position
        rect.right - rect.left,         // Width
        rect.bottom - rect.top,         // Height
        NULL,                            // Parent
        NULL,                            // Menu
        GetModuleHandle(NULL),
        gui                              // Create param (stored in window)
    );

    if (!gui->hwnd) {
        snprintf(gui->error_message, sizeof(gui->error_message),
                 "Failed to create window: error %lu", GetLastError());
        delete gui;
        return nullptr;
    }

    // Create and set plug frame before attaching
    gui->frame = new PlugFrame(gui->hwnd);
    gui->view->setFrame(gui->frame);

    // Attach the plugin view to our window
    if (gui->view->attached(reinterpret_cast<void*>(gui->hwnd),
                            kPlatformTypeHWND) != kResultTrue) {
        snprintf(gui->error_message, sizeof(gui->error_message),
                 "Failed to attach plugin view to window");
        delete gui;
        return nullptr;
    }

    return gui;
}

void rack_vst3_gui_free(RackVST3Gui* gui) {
    if (gui) {
        delete gui;
    }
}

int rack_vst3_gui_show(RackVST3Gui* gui, const char* title) {
    if (!gui || !gui->hwnd) {
        return RACK_VST3_ERROR_INVALID_PARAM;
    }

    // Set window title
    if (title) {
        // Convert UTF-8 to wide string
        int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
        if (len > 0) {
            wchar_t* wide_title = new wchar_t[len];
            MultiByteToWideChar(CP_UTF8, 0, title, -1, wide_title, len);
            SetWindowTextW(gui->hwnd, wide_title);
            delete[] wide_title;
        }
    }

    // Show the window
    ShowWindow(gui->hwnd, SW_SHOW);
    UpdateWindow(gui->hwnd);

    gui->visible = true;

    return RACK_VST3_OK;
}

int rack_vst3_gui_hide(RackVST3Gui* gui) {
    if (!gui || !gui->hwnd) {
        return RACK_VST3_ERROR_INVALID_PARAM;
    }

    ShowWindow(gui->hwnd, SW_HIDE);
    gui->visible = false;

    return RACK_VST3_OK;
}

int rack_vst3_gui_is_visible(RackVST3Gui* gui) {
    if (!gui) {
        return 0;
    }
    return gui->visible ? 1 : 0;
}

int rack_vst3_gui_get_size(RackVST3Gui* gui, uint32_t* width, uint32_t* height) {
    if (!gui || !width || !height) {
        return RACK_VST3_ERROR_INVALID_PARAM;
    }

    *width = gui->width;
    *height = gui->height;

    return RACK_VST3_OK;
}

int rack_vst3_gui_pump_events(RackVST3Gui* gui) {
    if (!gui || !gui->hwnd) {
        return RACK_VST3_ERROR_INVALID_PARAM;
    }

    int event_count = 0;
    MSG msg;

    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        event_count++;
    }

    return event_count;
}

unsigned long rack_vst3_gui_get_window_id(RackVST3Gui* gui) {
    if (!gui) {
        return 0;
    }
    return reinterpret_cast<unsigned long>(gui->hwnd);
}

} // extern "C"

#endif // _WIN32
