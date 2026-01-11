mod ffi;
mod util;
mod scanner;
mod instance;

#[cfg(any(target_os = "linux", target_os = "windows"))]
mod gui;

pub use scanner::Vst3Scanner;
pub use instance::Vst3Plugin;

#[cfg(any(target_os = "linux", target_os = "windows"))]
pub use gui::Vst3Gui;
