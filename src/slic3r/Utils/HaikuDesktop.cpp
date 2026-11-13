#ifdef __HAIKU__

#include "HaikuDesktop.hpp"

#include <cstring>
#include <sys/utsname.h>

#include <Entry.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <Mime.h>
#include <MimeType.h>
#include <Node.h>
#include <TypeConstants.h>
#include <fs_attr.h>
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

bool has_stale_zink_queue(const std::string &gl_renderer)
{
    std::string renderer = gl_renderer;
    for (char &c : renderer)
        c = char(tolower((unsigned char) c));
    if (renderer.find("zink") != std::string::npos)
        return false; // Zink renders: its queue is alive and well
    thread_info info;
    int32       cookie = 0;
    while (get_next_thread_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
        const char *colon = strrchr(info.name, ':');
        const char *name  = colon != nullptr ? colon + 1 : info.name;
        if (strncmp(name, "zfq", 3) == 0)
            return true;
    }
    return false;
}

static team_id get_team_id()
{
    thread_info info;
    return get_thread_info(find_thread(nullptr), &info) == B_OK ? info.team : -1;
}

void type_saved_file(const std::string &path)
{
    // What the application writes is known by its extension. The system's
    // guess would not do: a 3MF is a ZIP archive to the sniffer (and other
    // OPC formats start alike, so no rule can tell them apart), and the
    // registrar takes the extensions of a type registered while it runs into
    // account only after its next start (Haiku: Database::SetFileExtensions()
    // does not update its extension index).
    static const struct { const char *extension; const char *type; } known[] = {
        { ".3mf", "model/3mf" }, { ".stl", "model/stl" }, { ".step", "model/step" }, { ".stp", "model/step" },
        { ".obj", "model/obj" }, { ".amf", "application/x-amf" }, { ".gcode", "text/x-gcode" },
    };
    std::string lower = path;
    for (char &c : lower)
        c = char(tolower((unsigned char) c));
    for (const auto &k : known) {
        const size_t n = strlen(k.extension);
        if (lower.size() > n && lower.compare(lower.size() - n, n, k.extension) == 0) {
            BNode node(path.c_str());
            attr_info info;
            if (node.InitCheck() == B_OK && node.GetAttrInfo("BEOS:TYPE", &info) != B_OK)
                node.WriteAttr("BEOS:TYPE", B_MIME_STRING_TYPE, 0, k.type, strlen(k.type) + 1);
            return;
        }
    }
    update_mime_info(path.c_str(), false, true, B_UPDATE_MIME_INFO_NO_FORCE);
}

void register_document_types(const char *app_signature)
{
    app_info info;
    if (app_signature == nullptr) {
        // the signature of this application (its resources)
        if (be_roster->GetRunningAppInfo(find_thread(nullptr) >= 0 ? get_team_id() : -1, &info) != B_OK)
            return;
        app_signature = info.signature;
    }
    struct DocumentType
    {
        const char *type;
        const char *short_desc;
        const char *long_desc;
        std::vector<const char *> extensions;
        const char *sniffer_rule; // nullptr: the format has no reliable signature
    };
    // 3MF is a ZIP archive and STL/OBJ/G-code have no signature (an ASCII STL
    // starting "solid" is too weak a rule): those are known by extension.
    const DocumentType types[] = {
        { "model/3mf", "3MF model", "3D Manufacturing Format model or project", { "3mf" }, nullptr },
        { "model/stl", "STL model", "STL (stereolithography) 3D model", { "stl" }, nullptr },
        { "model/step", "STEP model", "STEP (ISO 10303-21) CAD model", { "step", "stp" }, "0.60 (\"ISO-10303-21;\")" },
        { "model/obj", "OBJ model", "Wavefront OBJ 3D model", { "obj" }, nullptr },
        { "application/x-amf", "AMF model", "Additive Manufacturing File Format model", { "amf" }, nullptr },
        { "text/x-gcode", "G-code", "G-code for 3D printers and CNC machines", { "gcode", "gco" }, nullptr },
    };
    for (const DocumentType &doc : types) {
        BMimeType mime(doc.type);
        if (mime.InitCheck() != B_OK)
            continue;
        if (!mime.IsInstalled() && mime.Install() != B_OK)
            continue;
        char text[B_MIME_TYPE_LENGTH];
        if (mime.GetShortDescription(text) != B_OK || text[0] == '\0')
            mime.SetShortDescription(doc.short_desc);
        if (mime.GetLongDescription(text) != B_OK || text[0] == '\0')
            mime.SetLongDescription(doc.long_desc);
        BMessage extensions;
        const char *known = nullptr;
        if (mime.GetFileExtensions(&extensions) != B_OK || extensions.FindString("extensions", &known) != B_OK) {
            BMessage add;
            for (const char *ext : doc.extensions)
                add.AddString("extensions", ext);
            mime.SetFileExtensions(&add);
        }
        if (doc.sniffer_rule != nullptr) {
            BString rule;
            if (mime.GetSnifferRule(&rule) != B_OK || rule.IsEmpty())
                mime.SetSnifferRule(doc.sniffer_rule);
        }
        if (mime.GetPreferredApp(text) != B_OK || text[0] == '\0')
            mime.SetPreferredApp(app_signature);
    }
}

} // namespace Haiku
} // namespace GUI
} // namespace Slic3r

#endif // __HAIKU__
