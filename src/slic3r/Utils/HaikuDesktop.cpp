#ifdef __HAIKU__

#include "HaikuDesktop.hpp"

#include <cstring>
#include <sys/utsname.h>

#include <Entry.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <OS.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <Url.h>

namespace Slic3r {
namespace GUI {
namespace Haiku {

static const char *kTrackerSignature = "application/x-vnd.Be-TRAK";

bool show_in_tracker(const std::string &path)
{
    BEntry entry(path.c_str(), true);
    if (entry.InitCheck() != B_OK || !entry.Exists())
        return false;

    BMessage message(B_REFS_RECEIVED);
    entry_ref ref;
    if (entry.IsDirectory()) {
        if (entry.GetRef(&ref) != B_OK)
            return false;
        message.AddRef("refs", &ref);
    } else {
        BEntry   parent;
        node_ref node;
        if (entry.GetParent(&parent) != B_OK || parent.GetRef(&ref) != B_OK)
            return false;
        message.AddRef("refs", &ref);
        // Tracker selects this node in the window it opens.
        if (entry.GetNodeRef(&node) == B_OK)
            message.AddData("nodeRefToSelect", B_RAW_TYPE, &node, sizeof(node));
    }

    BMessenger tracker(kTrackerSignature);
    if (!tracker.IsValid())
        return false;
    return tracker.SendMessage(&message) == B_OK;
}

bool open_path(const std::string &path)
{
    BEntry    entry(path.c_str(), true);
    entry_ref ref;
    if (entry.InitCheck() != B_OK || entry.GetRef(&ref) != B_OK)
        return false;
    if (entry.IsDirectory())
        return show_in_tracker(path);
    status_t status = be_roster->Launch(&ref);
    return status == B_OK || status == B_ALREADY_RUNNING;
}

bool open_url(const std::string &url)
{
    BUrl target(url.c_str(), true);
    if (!target.IsValid())
        return false;
    return target.OpenWithPreferredApplication() == B_OK;
}

// Key codes of the modifier keys on a PC keyboard (the key map's numbering).
static std::string modifier_label(uint32 modifier, const char *fallback)
{
    uint32 key = 0;
    if (get_modifier_key(modifier, &key) != B_OK)
        return fallback;
    switch (key) {
    case 0x5c: case 0x60: return "Ctrl";
    case 0x5d: case 0x5f: return "Alt";
    case 0x66: case 0x67: return "Win";
    default: return fallback;
    }
}

std::string command_key_label()
{
    return modifier_label(B_LEFT_COMMAND_KEY, "Alt");
}

std::string control_key_label()
{
    return modifier_label(B_LEFT_CONTROL_KEY, "Ctrl");
}

std::string os_description()
{
    struct utsname name;
    if (uname(&name) != 0)
        return "Haiku";
    // sysname "Haiku", release "1", version "hrev60207+2611 Nov 12 2026 ..."
    std::string version = name.version;
    std::string::size_type space = version.find(' ');
    if (space != std::string::npos)
        version.erase(space);
    return std::string(name.sysname) + " R" + name.release + " " + version + " " + name.machine;
}

std::string cpu_name()
{
    uint32 count = 0;
    if (get_cpu_topology_info(nullptr, &count) != B_OK || count == 0)
        return std::string();
    std::vector<cpu_topology_node_info> nodes(count);
    if (get_cpu_topology_info(nodes.data(), &count) != B_OK)
        return std::string();

    enum cpu_platform platform = B_CPU_UNKNOWN;
    enum cpu_vendor   vendor   = B_CPU_VENDOR_UNKNOWN;
    uint32            model    = 0;
    uint64            frequency = 0;
    for (uint32 i = 0; i < count; ++i) {
        switch (nodes[i].type) {
        case B_TOPOLOGY_ROOT: platform = nodes[i].data.root.platform; break;
        case B_TOPOLOGY_PACKAGE: vendor = nodes[i].data.package.vendor; break;
        case B_TOPOLOGY_CORE:
            model     = nodes[i].data.core.model;
            frequency = nodes[i].data.core.default_frequency;
            break;
        default: break;
        }
    }
    (void) model;

    const char *vendor_name = "";
    switch (vendor) {
    case B_CPU_VENDOR_AMD: vendor_name = "AMD"; break;
    case B_CPU_VENDOR_INTEL: vendor_name = "Intel"; break;
    case B_CPU_VENDOR_ARM: vendor_name = "ARM"; break;
    case B_CPU_VENDOR_BROADCOM: vendor_name = "Broadcom"; break;
    case B_CPU_VENDOR_APPLE: vendor_name = "Apple"; break;
    case B_CPU_VENDOR_QUALCOMM: vendor_name = "Qualcomm"; break;
    case B_CPU_VENDOR_NVIDIA: vendor_name = "NVIDIA"; break;
    default: break;
    }
    const char *platform_name = "";
    switch (platform) {
    case B_CPU_x86: platform_name = "x86"; break;
    case B_CPU_x86_64: platform_name = "x86_64"; break;
    case B_CPU_ARM: platform_name = "ARM"; break;
    case B_CPU_ARM_64: platform_name = "ARM64"; break;
    case B_CPU_RISC_V: platform_name = "RISC-V"; break;
    default: break;
    }
    std::string name = vendor_name;
    if (!name.empty() && platform_name[0])
        name += ' ';
    name += platform_name;
    if (frequency > 0)
        name += " " + std::to_string(frequency / 1000000) + " MHz";
    return name;
}

int cpu_count()
{
    system_info info;
    return get_system_info(&info) == B_OK ? (int) info.cpu_count : 1;
}

size_t memory_mb()
{
    system_info info;
    if (get_system_info(&info) != B_OK)
        return 0;
    return (size_t) (((uint64) info.max_pages * B_PAGE_SIZE) >> 20);
}

} // namespace Haiku
} // namespace GUI
} // namespace Slic3r

#endif // __HAIKU__
