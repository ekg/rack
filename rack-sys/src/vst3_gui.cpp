// VST3 GUI support for Linux/X11
// This file implements the GUI API for hosting VST3 plugin UIs on Linux

#include "rack_vst3.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/base/funknown.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <cstdio>
#include <cstring>
#include <atomic>

using namespace Steinberg;

// Forward declaration - defined in vst3_instance.cpp
extern "C" void* rack_vst3_plugin_get_edit_controller(RackVST3Plugin* plugin);

// IPlugFrame implementation for resize callbacks
// This allows the plugin to request window size changes
class PlugFrame : public IPlugFrame {
public:
    PlugFrame(Display* display, Window window)
        : ref_count_(1)
        , display_(display)
        , window_(window)
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

        // Resize the X11 window
        if (display_ && window_) {
            XResizeWindow(display_, window_, width, height);
            XFlush(display_);
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
    Display* display_;
    Window window_;
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

    // X11 resources
    Display* display;
    Window window;
    Atom wm_delete_window;

    // State
    bool visible;
    uint32_t width;
    uint32_t height;

    // Error info
    char error_message[256];

    RackVST3Gui()
        : plugin(nullptr)
        , frame(nullptr)
        , display(nullptr)
        , window(0)
        , wm_delete_window(0)
        , visible(false)
        , width(0)
        , height(0)
    {
        error_message[0] = '\0';
    }

    ~RackVST3Gui() {
        // Detach view first if attached
        if (view && window) {
            view->removed();
        }
        view = nullptr;

        // Release frame
        if (frame) {
            frame->release();
            frame = nullptr;
        }

        // Destroy X11 window
        if (display && window) {
            XDestroyWindow(display, window);
        }

        // Close display connection
        if (display) {
            XCloseDisplay(display);
        }
    }
};

extern "C" {

RackVST3Gui* rack_vst3_gui_create(RackVST3Plugin* plugin) {
    if (!plugin) {
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

    // Check if the view supports X11
    if (plugView->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultTrue) {
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

    // Open X11 display
    gui->display = XOpenDisplay(nullptr);
    if (!gui->display) {
        snprintf(gui->error_message, sizeof(gui->error_message),
                 "Failed to open X11 display");
        delete gui;
        return nullptr;
    }

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

    // Get default screen
    int screen = DefaultScreen(gui->display);
    Window root = RootWindow(gui->display, screen);

    // Create window
    XSetWindowAttributes attrs;
    attrs.background_pixel = BlackPixel(gui->display, screen);
    attrs.border_pixel = BlackPixel(gui->display, screen);
    attrs.event_mask = ExposureMask | StructureNotifyMask | KeyPressMask |
                       KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                       PointerMotionMask | EnterWindowMask | LeaveWindowMask |
                       FocusChangeMask;

    gui->window = XCreateWindow(
        gui->display,
        root,
        0, 0,                    // x, y position
        gui->width, gui->height, // width, height
        0,                       // border width
        CopyFromParent,          // depth
        InputOutput,             // class
        CopyFromParent,          // visual
        CWBackPixel | CWBorderPixel | CWEventMask,
        &attrs
    );

    if (!gui->window) {
        snprintf(gui->error_message, sizeof(gui->error_message),
                 "Failed to create X11 window");
        delete gui;
        return nullptr;
    }

    // Set up window close handling
    gui->wm_delete_window = XInternAtom(gui->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(gui->display, gui->window, &gui->wm_delete_window, 1);

    // Create and set plug frame before attaching
    gui->frame = new PlugFrame(gui->display, gui->window);
    gui->view->setFrame(gui->frame);

    // Attach the plugin view to our X11 window
    // The VST3 spec says to pass the X11 window ID as a void pointer
    if (gui->view->attached(reinterpret_cast<void*>(gui->window),
                            kPlatformTypeX11EmbedWindowID) != kResultTrue) {
        snprintf(gui->error_message, sizeof(gui->error_message),
                 "Failed to attach plugin view to X11 window");
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
    if (!gui || !gui->display || !gui->window) {
        return RACK_VST3_ERROR_INVALID_PARAM;
    }

    // Set window title
    const char* window_title = title ? title : "VST3 Plugin";
    XStoreName(gui->display, gui->window, window_title);

    // Also set _NET_WM_NAME for modern window managers
    Atom net_wm_name = XInternAtom(gui->display, "_NET_WM_NAME", False);
    Atom utf8_string = XInternAtom(gui->display, "UTF8_STRING", False);
    XChangeProperty(gui->display, gui->window, net_wm_name, utf8_string, 8,
                   PropModeReplace, reinterpret_cast<const unsigned char*>(window_title),
                   strlen(window_title));

    // Map (show) the window
    XMapWindow(gui->display, gui->window);
    XFlush(gui->display);

    gui->visible = true;

    return RACK_VST3_OK;
}

int rack_vst3_gui_hide(RackVST3Gui* gui) {
    if (!gui || !gui->display || !gui->window) {
        return RACK_VST3_ERROR_INVALID_PARAM;
    }

    XUnmapWindow(gui->display, gui->window);
    XFlush(gui->display);

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
    if (!gui || !gui->display) {
        return RACK_VST3_ERROR_INVALID_PARAM;
    }

    int event_count = 0;

    while (XPending(gui->display)) {
        XEvent event;
        XNextEvent(gui->display, &event);
        event_count++;

        switch (event.type) {
            case ConfigureNotify: {
                // Window resized or moved
                XConfigureEvent& xce = event.xconfigure;
                if (xce.width != static_cast<int>(gui->width) ||
                    xce.height != static_cast<int>(gui->height)) {

                    gui->width = xce.width;
                    gui->height = xce.height;

                    // Notify the plugin view of the new size
                    if (gui->view) {
                        ViewRect newRect(0, 0, gui->width, gui->height);
                        gui->view->onSize(&newRect);
                    }
                }
                break;
            }

            case Expose: {
                // Window needs redrawing - the plugin handles its own drawing
                // but we may need to flush
                XFlush(gui->display);
                break;
            }

            case ClientMessage: {
                // Check for window close request
                if (static_cast<Atom>(event.xclient.data.l[0]) == gui->wm_delete_window) {
                    // User clicked the close button
                    rack_vst3_gui_hide(gui);
                }
                break;
            }

            case FocusIn: {
                // Window gained focus
                break;
            }

            case FocusOut: {
                // Window lost focus
                break;
            }

            default:
                // Let X11 handle other events
                break;
        }
    }

    return event_count;
}

unsigned long rack_vst3_gui_get_window_id(RackVST3Gui* gui) {
    if (!gui) {
        return 0;
    }
    return gui->window;
}

} // extern "C"
