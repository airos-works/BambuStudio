#ifdef __HAIKU__

#include "HaikuUtils.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <FindDirectory.h>
#include <OS.h>
#include <image.h>

#include <dlfcn.h>
#include <locale.h>
#include <pthread.h>
#include <wchar.h>

// --- setlocale(), serialized and cheap ----------------------------------------------------
//
// libstdc++ is built with GCC's "generic" locale model on Haiku: every floating point number
// read from or written to a stream (operator>>(double&), operator<<(double)) switches the
// locale of the *process* to "C" and back:
//
//     old = setlocale(LC_ALL, NULL); setlocale(LC_ALL, "C"); strtod(...); setlocale(LC_ALL, old);
//
// The slicer does that on all cores at once (the presets are parsed in a tbb::parallel_for).
// libroot's setlocale() is not thread safe, and once the process has a locale other than "C"
// each call rebuilds ICU objects: the application crashed at start-up now and then (in
// ICUCollateData::SetToPosix(), called by two workers) and spent seconds there.
//
// This definition is found before libroot's by every image of the process (the runtime
// loader resolves in load order, the executable first). It
//  - lets one thread at a time into the real function,
//  - returns the names in a buffer of the calling thread,
//  - does nothing where nothing changes: a category set to the name it has, and LC_ALL set
//    to "C" while LC_NUMERIC is "C" already. The latter is libstdc++'s switch for a number
//    conversion; GUI_App and the worker threads keep LC_NUMERIC at "C" at all times. The
//    restoring call that follows names the locale that never changed and is a no-op too.
namespace {

typedef char *(*setlocale_function)(int, const char *);

pthread_mutex_t s_setlocale_lock = PTHREAD_MUTEX_INITIALIZER;

// The name of every category as libroot reports it, valid until a category is changed.
struct LocaleName
{
    bool valid;
    char name[512];
};
LocaleName s_locale_names[LC_LAST + 1];

setlocale_function libroot_setlocale()
{
    static setlocale_function function = (setlocale_function) dlsym(RTLD_NEXT, "setlocale");
    return function;
}

// With s_setlocale_lock held.
const char *current_locale_name(int category)
{
    LocaleName &entry = s_locale_names[category];
    if (!entry.valid) {
        const char *name = libroot_setlocale()(category, nullptr);
        if (name == nullptr || strlen(name) >= sizeof(entry.name))
            return name;
        strcpy(entry.name, name);
        entry.valid = true;
    }
    return entry.name;
}

bool is_c_locale_name(const char *name)
{
    return name != nullptr && (strcmp(name, "C") == 0 || strcmp(name, "POSIX") == 0);
}

} // namespace

extern "C" __attribute__((visibility("default"), used)) char *setlocale(int category, const char *locale)
{
    static thread_local char t_result[512];

    if (category < 0 || category > LC_LAST || libroot_setlocale() == nullptr)
        return nullptr;

    pthread_mutex_lock(&s_setlocale_lock);
    const char *result;
    if (locale == nullptr) {
        result = current_locale_name(category);
    } else if (*locale != 0 && current_locale_name(category) != nullptr && strcmp(locale, current_locale_name(category)) == 0) {
        // the category has this locale already
        result = current_locale_name(category);
    } else if (category == LC_ALL && is_c_locale_name(locale) && is_c_locale_name(current_locale_name(LC_NUMERIC))) {
        // the switch around a number conversion: numbers are in the "C" locale already
        result = "C";
    } else {
        result = libroot_setlocale()(category, locale);
        for (LocaleName &entry : s_locale_names)
            entry.valid = false;
    }
    if (result != nullptr) {
        if (strlen(result) < sizeof(t_result))
            result = strcpy(t_result, result);
    }
    pthread_mutex_unlock(&s_setlocale_lock);
    return const_cast<char *>(result);
}

// --- mbstowcs(), wcstombs(): thread safe ----------------------------------------------------
//
// libroot's mbstowcs() and wcstombs() keep their conversion state in one static mbstate_t
// (posix/wchar/mbstowcs.c, wcstombs.c), and with the ICU locale backend that state holds the
// ICU converter: two threads converting at once share one converter and crash in
// ucnv_getNextUChar(). wxWidgets converts every char string through mbstowcs() (wxConvLibc),
// on the main thread and on workers alike: the camera thread of the Device tab
// (MediaPlayCtrl::media_proc(), building a wxURI) crashed so while the main thread was busy.
// POSIX does not allow these two to share state between threads. These definitions, found
// before libroot's like setlocale() above, give each thread its own state.
extern "C" __attribute__((visibility("default"), used)) size_t mbstowcs(wchar_t *dest, const char *source, size_t length)
{
    static thread_local mbstate_t t_state;
    return mbsrtowcs(dest, &source, length, &t_state);
}

extern "C" __attribute__((visibility("default"), used)) size_t wcstombs(char *dest, const wchar_t *source, size_t length)
{
    static thread_local mbstate_t t_state;
    return wcsrtombs(dest, &source, length, &t_state);
}

namespace Slic3r {
namespace Haiku {

static bool is_directory(const std::string &path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string parent_path(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos || slash == 0 ? std::string() : path.substr(0, slash);
}

std::string executable_path()
{
    char path[B_PATH_NAME_LENGTH];
    if (find_path(B_APP_IMAGE_SYMBOL, B_FIND_PATH_IMAGE_PATH, nullptr, path, sizeof(path)) == B_OK)
        return path;
    return {};
}

std::string resources_dir(const std::string &app_name)
{
    // an application folder, or the build tree
    std::string dir = parent_path(executable_path());
    for (int level = 0; level < 2 && !dir.empty(); ++level, dir = parent_path(dir))
        if (is_directory(dir + "/resources"))
            return dir + "/resources";
    // a package: the data directory of the installation location it is in
    char path[B_PATH_NAME_LENGTH];
    const std::string sub_path = app_name + "/resources";
    if (find_path(B_APP_IMAGE_SYMBOL, B_FIND_PATH_DATA_DIRECTORY, sub_path.c_str(), path, sizeof(path)) == B_OK && is_directory(path))
        return path;
    // or any installation location that has them
    char  **paths = nullptr;
    size_t  count = 0;
    std::string found;
    if (find_paths_etc(nullptr, B_FIND_PATH_DATA_DIRECTORY, sub_path.c_str(), B_FIND_PATH_EXISTING_ONLY, &paths, &count) == B_OK) {
        if (count > 0)
            found = paths[0];
        free(paths);
    }
    return found;
}

std::string user_settings_dir(const std::string &app_name)
{
    char path[B_PATH_NAME_LENGTH];
    if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, path, sizeof(path)) != B_OK)
        return {};
    return std::string(path) + "/" + app_name;
}

std::vector<std::string> font_directories()
{
    std::vector<std::string> dirs;
    char  **paths = nullptr;
    size_t  count = 0;
    if (find_paths_etc(nullptr, B_FIND_PATH_FONTS_DIRECTORY, nullptr, B_FIND_PATH_EXISTING_ONLY, &paths, &count) == B_OK) {
        dirs.assign(paths, paths + count);
        free(paths);
    }
    return dirs;
}

static void scan_fonts(FT_Library library, const std::string &dir, std::vector<FontFace> &faces, int depth)
{
    DIR *handle = opendir(dir.c_str());
    if (handle == nullptr)
        return;
    while (const dirent *entry = readdir(handle)) {
        if (entry->d_name[0] == '.')
            continue;
        const std::string path = dir + "/" + entry->d_name;
        struct stat       st;
        if (::stat(path.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 4)
                scan_fonts(library, path, faces, depth + 1);
            continue;
        }
        // the faces of the file (more than one in a collection)
        for (FT_Long index = 0, count = 1; index < count; ++index) {
            FT_Face face;
            if (FT_New_Face(library, path.c_str(), index, &face) != 0)
                break;
            count = face->num_faces;
            if (FT_IS_SCALABLE(face) && face->family_name != nullptr)
                faces.push_back({ face->family_name, face->style_name != nullptr ? face->style_name : "", path, static_cast<int>(index),
                                  (face->style_flags & FT_STYLE_FLAG_BOLD) != 0, (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0 });
            FT_Done_Face(face);
        }
    }
    closedir(handle);
}

const std::vector<FontFace> &installed_fonts(bool rescan)
{
    static std::mutex            mutex;
    static std::vector<FontFace> fonts;
    static bool                  scanned = false;
    std::lock_guard<std::mutex>  lock(mutex);
    if (scanned && !rescan)
        return fonts;
    std::vector<FontFace> faces;
    FT_Library            library;
    if (FT_Init_FreeType(&library) == 0) {
        for (const std::string &dir : font_directories())
            scan_fonts(library, dir, faces, 0);
        FT_Done_FreeType(library);
    }
    std::sort(faces.begin(), faces.end(), [](const FontFace &a, const FontFace &b) {
        const int family = strcasecmp(a.family.c_str(), b.family.c_str());
        return family != 0 ? family < 0 : a.style < b.style;
    });
    fonts.swap(faces);
    scanned = true;
    return fonts;
}

const FontFace *find_font(const std::string &family, bool bold, bool italic)
{
    const FontFace *best       = nullptr;
    int             best_score = -1;
    for (const FontFace &face : installed_fonts()) {
        if (strcasecmp(face.family.c_str(), family.c_str()) != 0)
            continue;
        // bold and italic as asked; among those the plainest style name
        // ("Regular" before "Light", "Bold" before "ExtraBold Condensed")
        int score = (face.bold == bold ? 4 : 0) + (face.italic == italic ? 4 : 0);
        if (strcasecmp(face.style.c_str(), "Regular") == 0 || strcasecmp(face.style.c_str(), "Bold") == 0 ||
            strcasecmp(face.style.c_str(), "Italic") == 0 || strcasecmp(face.style.c_str(), "Bold Italic") == 0)
            score += 2;
        if (score > best_score) {
            best_score = score;
            best       = &face;
        }
    }
    return best;
}

std::string team_name(int team)
{
    // the name of a team is its command line: the path of its application
    // image is the reliable one
    image_info info;
    int32      cookie = 0;
    const team_id id = team == 0 ? B_CURRENT_TEAM : static_cast<team_id>(team);
    while (get_next_image_info(id, &cookie, &info) == B_OK)
        if (info.type == B_APP_IMAGE) {
            const std::string path = info.name;
            const size_t      slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    return {};
}

size_t physical_memory()
{
    system_info info;
    if (get_system_info(&info) != B_OK)
        return 0;
    return static_cast<size_t>(info.max_pages) * B_PAGE_SIZE;
}

size_t free_memory()
{
    system_info info;
    if (get_system_info(&info) != B_OK || info.used_pages > info.max_pages)
        return 0;
    return static_cast<size_t>(info.max_pages - info.used_pages) * B_PAGE_SIZE;
}

size_t resident_memory()
{
    size_t    total  = 0;
    area_info info;
    ssize_t   cookie = 0;
    while (get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK)
        total += info.ram_size;
    return total;
}

} // namespace Haiku
} // namespace Slic3r

#endif // __HAIKU__
