// The headless slicer: Bambu Studio's slicing pipeline on libslic3r alone,
// for builds without the GUI (SLIC3R_GUI=OFF).
//
// The command line mode of BambuStudio.cpp is part of the application: it
// keeps its plates in the GUI's PartPlateList and renders thumbnails with
// OpenGL, so it cannot be built without wxWidgets and a GL context. This
// program does the part that needs neither: load models, take the printer,
// process and filament settings from the system presets (or from preset
// files), orient, arrange, slice one plate and write the G-code and the
// sliced project (.gcode.3mf). The options it shares with BambuStudio.cpp
// have the same names and meanings; they are parsed with the same
// definitions (cli_*_config_def).
// The one definition of the SVG parser that libslic3r declares (the GUI
// library has it in BitmapCache.cpp); before any header that includes its
// declarations.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"

#include <iostream>
#include <string>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/integration/filesystem.hpp>
#include <boost/nowide/iostream.hpp>

#include <tbb/task_arena.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Config.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Log.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/Orient.hpp"
#include "libslic3r/Platform.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Utils.hpp"
#ifdef __HAIKU__
#include "libslic3r/HaikuUtils.hpp"
#endif

using namespace Slic3r;
namespace fs = boost::filesystem;

namespace {

// The options of this program that BambuStudio.cpp does not have.
struct Options
{
    std::string              vendor = "BBL";
    std::string              printer;
    std::string              process;
    std::vector<std::string> filaments;
    std::string              resources;
    bool                     list_presets  = false;
    bool                     platform_info = false;
    int                      threads       = 0;
};

void usage()
{
    boost::nowide::cout <<
        SLIC3R_APP_NAME " " SLIC3R_VERSION " headless slicer\n"
        "Usage: bambu-studio-cli [options] model.stl|.3mf|.step|.obj ...\n"
        "\n"
        "Settings (system presets of resources/profiles):\n"
        "  --printer NAME          printer preset, e.g. \"Bambu Lab A1 0.4 nozzle\"\n"
        "  --process NAME          process preset (default: the printer's)\n"
        "  --filament NAME[;NAME]  filament presets, one per filament (default: the printer's)\n"
        "  --vendor NAME           vendor of the presets (default BBL)\n"
        "  --list-presets          list the vendor's printers, or with --printer the\n"
        "                          processes and filaments compatible with it\n"
        "  --load-settings \"machine.json;process.json\"\n"
        "  --load-filaments \"filament1.json;...\"\n"
        "                          complete preset files, applied over the presets\n"
        "  --OPTION VALUE          any print setting, e.g. --curr-bed-type \"Textured PEI Plate\"\n"
        "\n"
        "Actions:\n"
        "  --orient 1              orient the objects for printing\n"
        "  --arrange 0|1           arrange the objects on the plate (default 1; 0 keeps positions)\n"
        "  --slice 1               slice the plate\n"
        "  --outputdir DIR         where plate_1.gcode goes (default: the current directory)\n"
        "  --export-3mf FILE       write the sliced project (G-code inside) as FILE\n"
        "  --info                  print what the models contain\n"
        "\n"
        "Other:\n"
        "  --resources-dir DIR     the resources directory (default: found from the executable)\n"
        "  --datadir DIR           the configuration directory\n"
        "  --threads N             limit the worker threads\n"
        "  --platform-info         print what the program finds out about the system\n"
        "  --debug N               log level 0 (fatal) to 5 (trace), default 1\n"
        "  --help\n";
}

// Takes this program's own options out of argv; false on a missing value.
bool take_own_options(std::vector<char *> &args, Options &opts)
{
    std::vector<char *> rest;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string arg = args[i];
        auto value = [&](std::string &out) {
            if (i + 1 >= args.size())
                return false;
            out = args[++i];
            return true;
        };
        std::string v;
        if (arg == "--printer") {
            if (!value(opts.printer)) return false;
        } else if (arg == "--process") {
            if (!value(opts.process)) return false;
        } else if (arg == "--vendor") {
            if (!value(opts.vendor)) return false;
        } else if (arg == "--resources-dir") {
            if (!value(opts.resources)) return false;
        } else if (arg == "--filament") {
            if (!value(v)) return false;
            std::vector<std::string> names;
            boost::split(names, v, [](char c) { return c == ';'; });
            for (const std::string &name : names)
                if (!name.empty())
                    opts.filaments.push_back(name);
        } else if (arg == "--threads") {
            if (!value(v)) return false;
            opts.threads = std::atoi(v.c_str());
        } else if (arg == "--list-presets")
            opts.list_presets = true;
        else if (arg == "--platform-info")
            opts.platform_info = true;
        else
            rest.push_back(args[i]);
    }
    args.swap(rest);
    return true;
}

std::string find_resources_dir()
{
#ifdef __HAIKU__
    return Haiku::resources_dir(SLIC3R_APP_KEY);
#else
    // "resources" beside the directory of the executable (bin/), as BambuStudio.cpp.
    try {
        const fs::path exe = fs::canonical(boost::dll::program_location());
        return (exe.parent_path().parent_path() / "resources").string();
    } catch (const std::exception &) {
        return {};
    }
#endif
}

// What the platform layer reports: paths, memory, threads, fonts.
void print_platform_info()
{
    boost::nowide::cout << "platform: " << platform_to_string(platform()) << " / " << platform_flavor_to_string(platform_flavor()) << "\n"
                        << "hardware threads: " << tbb::this_task_arena::max_concurrency() << "\n"
                        << "physical memory: " << total_physical_memory() / (1024 * 1024) << " MB\n"
                        << "peak memory of this process: " << static_cast<long>(get_peak_rss_mb()) << " MB\n"
                        << "process name: " << get_process_name(0) << "\n"
                        << "thread name: " << get_current_thread_name().value_or("?") << "\n"
                        << "resources: " << resources_dir() << "\n"
                        << "data directory: " << data_dir() << "\n";
#ifdef __HAIKU__
    boost::nowide::cout << "executable: " << Haiku::executable_path() << "\n"
                        << "free memory: " << Haiku::free_memory() / (1024 * 1024) << " MB\n";
    for (const std::string &dir : Haiku::font_directories())
        boost::nowide::cout << "font directory: " << dir << "\n";
    const std::vector<Haiku::FontFace> &fonts = Haiku::installed_fonts();
    boost::nowide::cout << "installed font faces: " << fonts.size() << "\n";
    if (!fonts.empty())
        for (bool bold : { false, true })
            if (const Haiku::FontFace *face = Haiku::find_font(fonts.front().family, bold, false))
                boost::nowide::cout << "font \"" << face->family << "\"" << (bold ? " bold" : "") << ": " << face->style << ", " << face->path << "\n";
#endif
}

void list_presets(PresetBundle &bundle, const Options &opts)
{
    if (opts.printer.empty()) {
        for (const Preset &preset : bundle.printers.get_presets())
            if (preset.is_system && preset.is_visible)
                boost::nowide::cout << preset.name << "\n";
        return;
    }
    boost::nowide::cout << "printer: " << bundle.printers.get_selected_preset().name << "\n";
    for (const Preset &preset : bundle.prints.get_presets())
        if (preset.is_system && preset.is_visible && preset.is_compatible)
            boost::nowide::cout << "process: " << preset.name << "\n";
    for (const Preset &preset : bundle.filaments.get_presets())
        if (preset.is_system && preset.is_visible && preset.is_compatible)
            boost::nowide::cout << "filament: " << preset.name << "\n";
}

// The full configuration of the selected system presets, as the application
// composes it for slicing.
bool config_from_presets(PresetBundle &bundle, Options &opts, DynamicPrintConfig &config)
{
    const std::string profiles = (fs::path(resources_dir()) / "profiles").string();
    try {
        bundle.load_vendor_configs_from_json(profiles, opts.vendor, PresetBundle::LoadSystem, ForwardCompatibilitySubstitutionRule::EnableSilent);
    } catch (const std::exception &e) {
        boost::nowide::cerr << "Cannot load the " << opts.vendor << " presets from " << profiles << ": " << e.what() << std::endl;
        return false;
    }
    if (opts.printer.empty())
        return true; // --list-presets of the printers
    if (!bundle.printers.select_preset_by_name(opts.printer, true) || bundle.printers.get_selected_preset().name != opts.printer) {
        boost::nowide::cerr << "No printer preset \"" << opts.printer << "\" (see --list-presets)" << std::endl;
        return false;
    }
    const DynamicPrintConfig &printer = bundle.printers.get_selected_preset().config;
    if (opts.process.empty())
        opts.process = printer.opt_string("default_print_profile");
    if (opts.filaments.empty())
        if (const auto *names = printer.option<ConfigOptionStrings>("default_filament_profile"); names != nullptr && !names->values.empty())
            opts.filaments.push_back(names->values.front());
    if (!bundle.prints.select_preset_by_name(opts.process, true) || bundle.prints.get_selected_preset().name != opts.process) {
        boost::nowide::cerr << "No process preset \"" << opts.process << "\" (see --list-presets --printer ...)" << std::endl;
        return false;
    }
    if (opts.filaments.empty()) {
        boost::nowide::cerr << "No filament given and the printer preset names no default one" << std::endl;
        return false;
    }
    for (const std::string &name : opts.filaments)
        if (bundle.filaments.find_preset(name, false) == nullptr) {
            boost::nowide::cerr << "No filament preset \"" << name << "\" (see --list-presets --printer ...)" << std::endl;
            return false;
        }
    bundle.filaments.select_preset_by_name(opts.filaments.front(), true);
    bundle.set_num_filaments(static_cast<unsigned int>(opts.filaments.size()));
    bundle.filament_presets = opts.filaments;
    bundle.update_compatible(PresetSelectCompatibleType::Never);
    if (opts.list_presets)
        return true;
    if (!bundle.prints.get_selected_preset().is_compatible)
        boost::nowide::cerr << "Warning: process \"" << opts.process << "\" is not marked compatible with the printer" << std::endl;
    config = bundle.full_config();
    return true;
}

// Applies complete preset files (as BambuStudio.cpp's --load-settings and
// --load-filaments take them) over the configuration.
bool apply_preset_files(const std::vector<std::string> &files, DynamicPrintConfig &config, bool filaments)
{
    size_t filament_index = 0;
    for (const std::string &file : files) {
        if (file.empty())
            continue;
        DynamicPrintConfig                 loaded;
        std::map<std::string, std::string> key_values;
        std::string                        reason;
        try {
            loaded.load_from_json(file, ForwardCompatibilitySubstitutionRule::Enable, key_values, reason);
        } catch (const std::exception &e) {
            boost::nowide::cerr << "Cannot load " << file << ": " << e.what() << std::endl;
            return false;
        }
        if (!reason.empty()) {
            boost::nowide::cerr << "Cannot load " << file << ": " << reason << std::endl;
            return false;
        }
        if (!filaments) {
            config.apply(loaded, true);
            continue;
        }
        // one filament: its values go to this filament's place in the per-filament vectors
        for (const std::string &key : loaded.keys()) {
            const ConfigOption *source = loaded.option(key);
            ConfigOption       *target = config.option(key, true);
            if (source->is_vector() && target->is_vector() && key != "compatible_printers" && key != "compatible_prints") {
                auto *vector = static_cast<ConfigOptionVectorBase *>(target);
                if (vector->size() <= filament_index)
                    vector->resize(filament_index + 1, source);
                vector->set_at(source, filament_index, 0);
            } else if (filament_index == 0)
                target->set(source);
        }
        ++filament_index;
    }
    return true;
}

void print_info(const Model &model)
{
    size_t index = 0;
    for (const ModelObject *object : model.objects) {
        const BoundingBoxf3 box = object->bounding_box();
        size_t facets = 0;
        for (const ModelVolume *volume : object->volumes)
            facets += volume->mesh().facets_count();
        boost::nowide::cout << boost::format("object %1%: \"%2%\", %3% volume(s), %4% instance(s), %5% facets, size %6$.2f x %7$.2f x %8$.2f mm\n")
            % ++index % object->name % object->volumes.size() % object->instances.size() % facets % box.size().x() % box.size().y() % box.size().z();
    }
}

// Arranges every instance on the one plate, as the application does: the
// items carry their filaments' temperatures and their brims and skirts, the
// printer's excluded areas stay free.
bool arrange_on_plate(Model &model, const DynamicPrintConfig &config)
{
    arrangement::ArrangePolygons selected, excluded;
    for (ModelObject *object : model.objects)
        for (ModelInstance *instance : object->instances) {
            arrangement::ArrangePolygon item = get_instance_arrange_poly(instance, config);
            item.itemid = static_cast<int>(selected.size());
            selected.emplace_back(std::move(item));
        }

    arrangement::ArrangeParams params;
    params.allow_rotations  = true;
    params.min_obj_distance = 0;
    params.printable_height = config.opt_float("printable_height");
    if (const auto *sequence = config.option<ConfigOptionEnum<PrintSequence>>("print_sequence"))
        params.is_seq_print = sequence->value == PrintSequence::ByObject;
    if (const auto *value = config.option<ConfigOptionFloat>("extruder_clearance_height_to_rod"))
        params.clearance_height_to_rod = static_cast<float>(value->value);
    if (const auto *value = config.option<ConfigOptionFloat>("extruder_clearance_height_to_lid"))
        params.clearance_height_to_lid = static_cast<float>(value->value);
    if (const auto *value = config.option<ConfigOptionFloat>("extruder_clearance_max_radius"))
        params.cleareance_radius = static_cast<float>(value->value);
    if (const auto *structure = config.option<ConfigOptionEnum<PrinterStructure>>("printer_structure"))
        params.align_to_y_axis = structure->value == PrinterStructure::psI3;
    arrangement::update_arrange_params(params, config, selected);
    arrangement::update_selected_items_inflation(selected, config, params);

    if (const auto *area = config.option<ConfigOptionPoints>("bed_exclude_area"); area != nullptr && area->values.size() >= 3) {
        arrangement::ArrangePolygon region;
        for (const Vec2d &point : area->values)
            region.poly.contour.points.emplace_back(scaled(point.x()), scaled(point.y()));
        region.is_virt_object = true;
        region.bed_idx        = 0;
        region.height         = 1;
        region.name           = "ExcludedRegion0";
        region.inflation      = scaled(1.);
        excluded.emplace_back(std::move(region));
    }
    params.excluded_regions = excluded;

    try {
        arrangement::arrange(selected, excluded, get_bed_shape(config), params);
    } catch (const std::exception &e) {
        boost::nowide::cerr << "Arranging failed: " << e.what() << std::endl;
        return false;
    }
    for (arrangement::ArrangePolygon &item : selected) {
        if (item.bed_idx != 0) {
            boost::nowide::cerr << "\"" << item.name << "\" does not fit on the plate with the other objects" << std::endl;
            return false;
        }
        item.apply();
    }
    return true;
}

int run(int argc, char **argv)
{
    set_current_thread_name("bambustu_main");
    save_main_thread_id();
    boost::nowide::nowide_filesystem();
    detect_platform();

    Options             opts;
    std::vector<char *> args(argv, argv + argc);
    if (!take_own_options(args, opts)) {
        usage();
        return 1;
    }

    DynamicPrintAndCLIConfig cli;
    std::vector<std::string> input_files;
    t_config_option_keys     order;
    if (!cli.read_cli(static_cast<int>(args.size()), args.data(), &input_files, &order)) {
        usage();
        return 1;
    }
    if (cli.has("help") && cli.opt_bool("help")) {
        usage();
        return 0;
    }
    set_logging_level(cli.has("debug") ? cli.opt_int("debug") : 1);

    if (opts.resources.empty())
        opts.resources = find_resources_dir();
    if (opts.resources.empty() || !fs::is_directory(fs::path(opts.resources) / "profiles")) {
        boost::nowide::cerr << "The resources directory was not found" << (opts.resources.empty() ? "" : " at " + opts.resources)
                            << ": give it with --resources-dir" << std::endl;
        return 1;
    }
    const fs::path resources = fs::canonical(opts.resources);
    set_resources_dir(resources.string());
    set_var_dir((resources / "images").string());
    set_local_dir((resources / "i18n").string());
    set_sys_shapes_dir((resources / "shapes").string());
    set_temporary_dir(fs::temp_directory_path().string());
    if (cli.has("datadir") && !cli.opt_string("datadir").empty())
        set_data_dir(fs::absolute(cli.opt_string("datadir")).string());
#ifdef __HAIKU__
    else
        set_data_dir(Haiku::user_settings_dir(SLIC3R_APP_KEY));
#endif

    if (opts.platform_info) {
        print_platform_info();
        return 0;
    }

    // --- settings
    PresetBundle       bundle;
    DynamicPrintConfig config;
    const bool         use_presets = !opts.printer.empty() || opts.list_presets;
    if (use_presets && !config_from_presets(bundle, opts, config))
        return 1;
    if (opts.list_presets) {
        list_presets(bundle, opts);
        return 0;
    }

    // --- models
    if (input_files.empty()) {
        boost::nowide::cerr << "No model file given" << std::endl;
        usage();
        return 1;
    }
    Model          model;
    PlateDataPtrs  loaded_plates;
    bool           is_project = false;
    for (const std::string &file : input_files) {
        if (!fs::exists(file)) {
            boost::nowide::cerr << "No such file: " << file << std::endl;
            return 1;
        }
        try {
            DynamicPrintConfig         file_config;
            ConfigSubstitutionContext  substitutions(ForwardCompatibilitySubstitutionRule::Enable);
            std::vector<Preset *>      project_presets;
            bool                       is_bbl_3mf = false;
            Semver                     file_version;
            Model loaded;
            if (boost::algorithm::iends_with(file, ".step") || boost::algorithm::iends_with(file, ".stp"))
                // tessellated with the application's default deflections
                loaded = Model::read_from_step(file, LoadStrategy::LoadModel | LoadStrategy::AddDefaultInstances, nullptr, nullptr, nullptr,
                                               0.003, 0.5, false);
            else
                loaded = Model::read_from_file(file, &file_config, &substitutions,
                    LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances,
                    &loaded_plates, &project_presets, &is_bbl_3mf, &file_version);
            if (is_bbl_3mf && !file_config.empty() && model.objects.empty()) {
                is_project = true;
                // the project's own settings unless presets were asked for
                if (!use_presets)
                    config = file_config;
            }
            for (Preset *preset : project_presets)
                delete preset;
            for (ModelObject *object : loaded.objects)
                model.add_object(*object);
        } catch (const std::exception &e) {
            boost::nowide::cerr << "Cannot load " << file << ": " << e.what() << std::endl;
            return 1;
        }
    }
    if (model.objects.empty()) {
        boost::nowide::cerr << "The input files contain no objects" << std::endl;
        return 1;
    }
    model.add_default_instances();
    if (cli.has("info") && cli.opt_bool("info")) {
        print_info(model);
        if (!cli.has("slice") && !cli.has("export_3mf"))
            return 0;
    }

    if (cli.has("load_settings") && !apply_preset_files(cli.option<ConfigOptionStrings>("load_settings")->values, config, false))
        return 1;
    if (cli.has("load_filaments") && !apply_preset_files(cli.option<ConfigOptionStrings>("load_filaments")->values, config, true))
        return 1;
    if (config.empty()) {
        boost::nowide::cerr << "No settings: name a printer preset with --printer, or give preset files with --load-settings and --load-filaments" << std::endl;
        return 1;
    }
    // print settings given as options override the presets
    {
        DynamicPrintConfig full;
        full.apply(FullPrintConfig::defaults());
        full.apply(config);
        t_config_option_keys overrides;
        for (const std::string &key : order)
            if (print_config_def.has(key))
                overrides.push_back(key);
        full.apply_only(cli, overrides, true);
        config = std::move(full);
    }
    config.normalize_fdm();
    {
        const std::map<std::string, std::string> errors = config.validate(true);
        for (const auto &error : errors)
            boost::nowide::cerr << "Invalid setting " << error.first << ": " << error.second << std::endl;
        if (!errors.empty())
            return 1;
    }
    if (config.opt_enum<PrinterTechnology>("printer_technology") != ptFFF) {
        boost::nowide::cerr << "Unsupported printer technology (not FDM)" << std::endl;
        return 1;
    }

    // --- placement
    if (cli.has("orient") && cli.opt_int("orient") != 0)
        for (ModelObject *object : model.objects)
            orientation::orient(object);
    for (ModelObject *object : model.objects)
        object->ensure_on_bed();
    const bool arrange = cli.has("arrange") ? cli.opt_int("arrange") != 0 : !is_project;
    if (arrange && !arrange_on_plate(model, config))
        return 1;

    const bool slice = cli.has("slice") || cli.has("export_3mf");
    if (!slice)
        return 0;
    if (cli.has("slice") && cli.opt_int("slice") > 1) {
        boost::nowide::cerr << "The headless slicer slices plate 1 only" << std::endl;
        return 1;
    }

    // --- slicing
    std::string outputdir = cli.has("outputdir") ? cli.opt_string("outputdir") : std::string(".");
    fs::create_directories(outputdir);
    const std::string gcode_path = (fs::path(outputdir) / "plate_1.gcode").string();

    Print                print;
    GCodeProcessorResult result;
    print.set_plate_index(0);
    print.set_status_callback([](const PrintBase::SlicingStatus &status) {
        BOOST_LOG_TRIVIAL(info) << "slicing: " << status.percent << "% " << status.text;
    });
    const long long start = Utils::get_current_milliseconds_time_monotonic();
    // --threads: the slicing runs in an arena of that many threads (libslic3r
    // sizes its work by the arena's concurrency).
    tbb::task_arena arena(opts.threads > 0 ? opts.threads : tbb::task_arena::automatic);
    int             threads = 0;
    bool            sliced  = false;
    arena.execute([&]() {
    threads = tbb::this_task_arena::max_concurrency();
    try {
        print.apply(model, config);
        std::vector<StringObjectException> warnings;
        const StringObjectException        error = print.validate(&warnings);
        for (const StringObjectException &warning : warnings)
            boost::nowide::cerr << "Warning: " << warning.string << std::endl;
        if (!error.string.empty()) {
            boost::nowide::cerr << "The plate cannot be sliced: " << error.string << std::endl;
            return;
        }
        if (print.empty()) {
            boost::nowide::cerr << "Nothing to print: no object is inside the print volume" << std::endl;
            return;
        }
        print.process();
        print.export_gcode(gcode_path, &result, nullptr);
        sliced = true;
    } catch (const std::exception &e) {
        boost::nowide::cerr << "Slicing failed: " << e.what() << std::endl;
    }
    });
    if (!sliced)
        return 1;
    const double seconds = (Utils::get_current_milliseconds_time_monotonic() - start) / 1000.;

    size_t layers = 0;
    for (const PrintObject *object : print.objects())
        layers = std::max(layers, object->layer_count());
    const PrintStatistics &stats      = print.print_statistics();
    const float            print_time = result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time;
    boost::nowide::cout << boost::format("sliced in %1$.1f s with %2% threads: %3% layers, print time %4%, filament %5$.2f m / %6$.2f g, %7% tool change(s)\n")
        % seconds % threads % layers % get_time_dhms(print_time)
        % (stats.total_used_filament / 1000.) % stats.total_weight % stats.total_toolchanges;
    boost::nowide::cout << "G-code: " << gcode_path << " (" << fs::file_size(gcode_path) << " bytes)\n";

    // --- the sliced project
    if (cli.has("export_3mf")) {
        std::string path = cli.opt_string("export_3mf");
        if (fs::path(path).is_relative() && cli.has("outputdir"))
            path = (fs::path(outputdir) / path).string();

        PlateData plate;
        plate.plate_index = 0;
        for (size_t o = 0; o < model.objects.size(); ++o)
            for (size_t i = 0; i < model.objects[o]->instances.size(); ++i)
                plate.objects_and_instances.emplace_back(static_cast<int>(o), static_cast<int>(i));
        plate.gcode_file       = gcode_path;
        plate.is_sliced_valid  = true;
        plate.gcode_prediction = std::to_string(static_cast<int>(print_time));
        plate.gcode_weight     = (boost::format("%1$.2f") % stats.total_weight).str();
        plate.toolpath_outside = result.toolpath_outside;
        plate.is_label_object_enabled = result.label_object_enabled;
        plate.is_support_used  = print.is_support_used();
        plate.filament_maps    = print.get_filament_maps();
        plate.parse_filament_info(&result);

        StoreParams params;
        params.path = path.c_str();
        params.model = &model;
        params.plate_data_list.push_back(&plate);
        params.config = &config;
        params.export_plate_idx = 0;
        params.strategy = SaveStrategy::Silence | SaveStrategy::WithGcode | SaveStrategy::SplitModel | SaveStrategy::ShareMesh;
        if (!store_bbs_3mf(params)) {
            boost::nowide::cerr << "Cannot write " << path << std::endl;
            return 1;
        }
        boost::nowide::cout << "Project: " << path << " (" << fs::file_size(path) << " bytes)\n";
    }
    for (PlateData *loaded : loaded_plates)
        delete loaded;
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    boost::nowide::args utf8_args(argc, argv);
    try {
        return run(argc, argv);
    } catch (const std::exception &e) {
        boost::nowide::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
