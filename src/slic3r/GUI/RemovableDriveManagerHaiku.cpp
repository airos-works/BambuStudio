#ifdef __HAIKU__

#include "RemovableDriveManagerHaiku.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <Directory.h>
#include <Drivers.h>
#include <Path.h>
#include <Volume.h>
#include <VolumeRoster.h>
#include <fs_info.h>
#include <fs_volume.h>

namespace Slic3r {
namespace GUI {
namespace Haiku {

std::vector<std::pair<std::string, std::string>> removable_volumes()
{
    std::vector<std::pair<std::string, std::string>> volumes;
    BVolumeRoster roster;
    BVolume       boot, volume;
    roster.GetBootVolume(&boot);
    while (roster.GetNextVolume(&volume) == B_OK) {
        if (volume == boot || !volume.IsPersistent() || !volume.IsRemovable() || volume.IsReadOnly())
            continue;
        if (volume.FreeBytes() <= 0)
            continue;
        BDirectory root;
        BPath      path;
        char       name[B_FILE_NAME_LENGTH];
        if (volume.GetRootDirectory(&root) != B_OK || path.SetTo(&root, nullptr) != B_OK || volume.GetName(name) != B_OK)
            continue;
        volumes.emplace_back(name, path.Path());
    }
    return volumes;
}

bool eject_volume(const std::string &mount_point)
{
    // the device behind the volume, before it is gone
    std::string device;
    fs_info     info;
    const dev_t volume = dev_for_path(mount_point.c_str());
    if (volume >= 0 && fs_stat_dev(volume, &info) == 0)
        device = info.device_name;

    if (fs_unmount_volume(mount_point.c_str(), 0) != B_OK)
        return false;

    // Media that can be ejected (card readers, optical drives) are, once
    // no other partition of the device is mounted; the device refuses
    // otherwise. A USB stick is simply safe to pull out now.
    const size_t slash = device.find_last_of('/');
    if (slash != std::string::npos) {
        const std::string raw = device.substr(0, slash) + "/raw";
        const int         fd  = open(raw.c_str(), O_RDONLY);
        if (fd >= 0) {
            device_geometry geometry;
            if (ioctl(fd, B_GET_GEOMETRY, &geometry, sizeof(geometry)) == 0 && geometry.removable)
                ioctl(fd, B_EJECT_DEVICE);
            close(fd);
        }
    }
    return true;
}

} // namespace Haiku
} // namespace GUI
} // namespace Slic3r

#endif // __HAIKU__
