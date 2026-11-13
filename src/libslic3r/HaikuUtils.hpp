#ifndef slic3r_HaikuUtils_hpp_
#define slic3r_HaikuUtils_hpp_

// What libslic3r and the application ask the operating system on Haiku,
// answered with the kernel and storage kits' C API (libroot, no libbe).

#ifdef __HAIKU__

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r {
namespace Haiku {

// Full path of the application's executable (empty if unknown).
std::string executable_path();

// The directory with the application's resources (profiles, images, i18n):
// "resources" beside the executable or beside its directory (the application
// folder and the build tree), else data/<app_name>/resources of the
// installation location the application's package is activated in
// (/boot/system, ~/config, the non-packaged trees). Empty if there is none.
std::string resources_dir(const std::string &app_name);

// Where the application keeps its settings: <user settings>/<app_name>.
std::string user_settings_dir(const std::string &app_name);

// Every font directory: system and user, packaged and non-packaged.
std::vector<std::string> font_directories();

// One face of an installed scalable font. Haiku has no fontconfig, and the
// Interface Kit's BFont does not tell where a family's files are: the faces
// are read from the files in the font directories with FreeType.
struct FontFace
{
    std::string family;     // "Noto Sans"
    std::string style;      // "Bold Italic"
    std::string path;       // the font file
    int         face_index; // of the face in the file (collections)
    bool        bold;
    bool        italic;
};

// The installed faces, sorted by family and style. Scanned on the first
// call; rescan = true reads the directories again.
const std::vector<FontFace> &installed_fonts(bool rescan = false);

// The file of the face of a family (compared without case) that fits bold
// and italic best, nullptr if the family is not installed.
const FontFace *find_font(const std::string &family, bool bold, bool italic);

// Name of a team (process); of this one for 0.
std::string team_name(int team);

// Installed RAM, in bytes.
size_t physical_memory();

// RAM no team uses now, in bytes.
size_t free_memory();

// RAM currently committed to this team's areas, in bytes.
size_t resident_memory();

} // namespace Haiku
} // namespace Slic3r

#endif // __HAIKU__

#endif // slic3r_HaikuUtils_hpp_
