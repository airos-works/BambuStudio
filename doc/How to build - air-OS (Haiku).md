# Bambu Studio on air/OS (Haiku)

This branch (`airos` of airos-works/BambuStudio) builds Bambu Studio for
air/OS, a fork of Haiku, on x86_64 and arm64. The port is native: no GTK,
Qt, X11, Wayland, Cairo, Pango, GStreamer, DBus, fontconfig or WebKitGTK.

| Upstream on Linux | air/OS |
|---|---|
| wxWidgets on GTK 3 | wxWidgets with a native Be API port, `wxHaiku` (wxUniversal controls drawn through BControlLook; BWindow, BView, BBitmap, BFont, BShape/BGradient for wxGraphicsContext), from airos-works/wxWidgets, pinned in `tools/haiku/wx.lock` |
| GLX/EGL canvas | wxGLCanvas on the GL kit (one real context per wxGLContext; frames presented through the GL kit, no per-frame read-back) |
| WebKitGTK | wxWebView on the system's Summit engine (`summit_webkit` package); web views are created on first use and released again |
| DBus single instance | B_SINGLE_LAUNCH: Tracker and second launches hand files to the running instance |
| fontconfig | font directories from `find_paths()`, indexed with FreeType |
| udev/hidapi, /media | BVolumeRoster for removable drives; hidapi stub |
| xdg-open, .desktop files | BRoster; MIME types shipped in the package (`src/platform/haiku/mime_db`) |
| OpenCV | `src/minicv`: the few functions the tree uses |
| GLFW, OpenVDB, OpenMeshCraft | not used (MCUT for booleans; OpenVDB served SLA hollowing only) |
| Node.js device page bundle | not built (`SLIC3R_DEVICE_PAGE=OFF`): it needs Bambu's network plug-in |

Bambu's proprietary network plug-in has no Haiku build. Cloud login, cloud
sync, the device pages and sending to printers are therefore hidden or
inactive (`GUI_App::network_plugin_supported()`); preparing, slicing,
previewing and exporting `.gcode.3mf`/G-code work fully offline, and preset
updates still come from Bambu's servers.

## Building (cross, on Linux)

Needs an air/OS cross SDK per architecture (`HAIKU_SDK`, default
`/data1/airos/sdk/<arch>`) and the extra development packages
(`AIROS_DEPS`, default `/data1/airos/deps/<arch>`). Everything is built
outside the source tree, under `BS_SCRATCH` (default
`/data1/scratch/bambustudio`).

```sh
tools/haiku/build-deps.sh x86_64      # static Boost, TBB, OCCT (26 toolkits), CGAL, GMP/MPFR, NLopt, assimp, GLEW ...
tools/haiku/build-wx.sh x86_64        # the pinned wxWidgets -> BS_SCRATCH/wx-pinned/x86_64
WX_PREFIX=$BS_SCRATCH/wx-pinned/x86_64 tools/haiku/build.sh x86_64 --gui
tools/haiku/build.sh x86_64           # without --gui: the headless slicer bambu-studio-cli
```

Replace `x86_64` with `arm64` for ARM boards (one binary for every board:
`-march=armv8-a -mtune=cortex-a72`). System libraries air/OS ships (zlib,
libpng, libjpeg, libtiff, expat, FreeType, curl, OpenSSL, FFmpeg, ICU) are
used as they are.

Packages are made by airos-works/airos-ci (`build_bambustudio` in
`apps/build-app-packages.sh`), which runs the three scripts above, stages the
resources with `tools/haiku/stage-resources.py` (without the 185 MB printer
error database `hms/`, which only the device pages use, and with oversized
pictures scaled down) and the MIME types with `tools/haiku/stage-mime-db.sh`.
`.github/workflows/airos.yml` builds both architectures on every push to
`airos` and publishes them to the rolling "latest" release.

## Installed layout

- `apps/BambuStudio/BambuStudio` (Deskbar: Applications > Bambu Studio)
- `data/BambuStudio/resources`
- `data/mime_db/model/{3mf,stl,step,obj}`, `text/x-gcode`, `application/x-amf`
- settings and logs: `~/config/settings/BambuStudio`

## Tested

- x86_64: QEMU, air/OS hrev60207+2637: install, first-run wizard, open by
  file type, 3D view, slicing, preview, export (G-code checksum verified),
  preferences, dark mode, quit and restart.
- arm64: Raspberry Pi 4 (4 GB, V3D through Mesa EGL) and a Cortex-A78C board:
  the same workflow; a 3DBenchy slices to the same G-code statistics as on
  x86_64 (about 28 s on the Pi 4). BambuStudio with a model loaded uses about
  570 MB on the Pi; the main window takes 1.4 s to create.
- `tools/haiku/vm/slice-test.sh <arch>`: headless slicing in QEMU (STL, 3MF,
  OBJ, STEP; 1, 2 and 4 threads give identical G-code).

## Known limits

- No network plug-in: no cloud, device monitoring, camera or printing over
  the network (a native LAN-mode sender would be a separate project).
- The software renderer "Software Pipe" (llvmpipe from HaikuPorts' Mesa 22)
  crashes on BambuStudio's first frame; machines without a GPU driver use
  the Zink add-on's softpipe fallback, which works but is slow.
- Command-line slicing with the GUI binary renders no thumbnails (upstream
  uses a hidden GLFW window); `bambu-studio-cli` is the headless tool.
