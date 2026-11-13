#ifndef slic3r_Utils_HaikuDesktop_hpp_
#define slic3r_Utils_HaikuDesktop_hpp_

// What the application asks the desktop on Haiku (air/OS), through the Be
// API (libbe): Tracker and the roster instead of xdg-open, the system
// information instead of lsb_release and lscpu.
// No wxWidgets here.

#ifdef __HAIKU__

#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace Haiku {

// Shows a folder in Tracker. For a file: its folder, with the file selected.
bool show_in_tracker(const std::string &path);

// Opens a file with its preferred application (a folder in Tracker).
bool open_path(const std::string &path);

// Opens a URL with the application that handles its scheme.
bool open_url(const std::string &url);

// The label of the physical key that is the system's shortcut key (B_COMMAND_KEY, what
// wxWidgets reports as Ctrl) or its control key (B_CONTROL_KEY, reported as Alt): "Alt",
// "Ctrl" or "Win", as the Keymap preferences have them. By default Alt is the shortcut key.
std::string command_key_label();
std::string control_key_label();

// "air/OS R1~beta6 hrev60207 x86_64"
std::string os_description();
// "Intel(R) Xeon(R) ...", cores, installed memory in MiB
std::string cpu_name();
int         cpu_count();
size_t      memory_mb();

} // namespace Haiku
} // namespace GUI
} // namespace Slic3r

#endif // __HAIKU__

#endif // slic3r_Utils_HaikuDesktop_hpp_
