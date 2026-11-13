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

// True when the GL renderer add-on left a dead Zink flush queue behind in this
// team: a thread named "...:zfq<N>" while the renderer is not Zink itself (the
// add-on found no Vulkan device and fell back to softpipe). Fixed in mesa-hgl
// 2e046a9; with an older add-on, giving back a web view's engine view aborts
// the process (ZINK-TEARDOWN). Call after OpenGL was initialised.
bool has_stale_zink_queue(const std::string &gl_renderer);

// Gives a file the application wrote its MIME type (BEOS:TYPE, by extension
// and content), as files saved by Haiku programs have, so that Tracker shows
// and opens it right. A type already set stays.
void type_saved_file(const std::string &path);

// Makes the document types the application opens known to the system where
// they are not: description, file extensions, a sniffer rule where the format
// has a signature, and this application as the preferred one. Only what a
// type lacks is filled in: types other applications defined or claimed keep
// their settings. Cheap; called at every start. nullptr: this application's
// signature.
void register_document_types(const char *app_signature = nullptr);

} // namespace Haiku
} // namespace GUI
} // namespace Slic3r

#endif // __HAIKU__

#endif // slic3r_Utils_HaikuDesktop_hpp_
