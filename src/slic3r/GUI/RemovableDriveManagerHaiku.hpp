#ifndef slic3r_GUI_RemovableDriveManagerHaiku_hpp_
#define slic3r_GUI_RemovableDriveManagerHaiku_hpp_

// The removable volumes of a Haiku system for RemovableDriveManager, from
// the Storage Kit (BVolumeRoster, fs_unmount_volume()). No wxWidgets here.

#ifdef __HAIKU__

#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace Haiku {

// Name and mount point of every writable volume on a removable device (USB
// sticks, SD cards), the boot volume excepted.
std::vector<std::pair<std::string, std::string>> removable_volumes();

// Unmounts the volume mounted at mount_point and, if its device has
// removable media, ejects it. False if the volume is busy or not found.
bool eject_volume(const std::string &mount_point);

} // namespace Haiku
} // namespace GUI
} // namespace Slic3r

#endif // __HAIKU__

#endif // slic3r_GUI_RemovableDriveManagerHaiku_hpp_
