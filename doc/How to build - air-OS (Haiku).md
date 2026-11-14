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
| Bambu's network plug-in (closed source, downloaded at first start) | [Open Bamboo Networking](https://github.com/ClusterM/open-bamboo-networking) by Alexey Cluster (AGPL-3.0, same C ABI), built from source and shipped in the package: `apps/BambuStudio/plugins` (fork airos-works/open-bamboo-networking, pinned in `tools/haiku/obn.lock`) |
| GStreamer camera pipeline | none: the camera view decodes with the system's FFmpeg (`wxMediaCtrl3`, as upstream on Linux) |
| Node.js device page bundle | not built (`SLIC3R_DEVICE_PAGE=OFF`, no Node.js at build time): the Device tab's AMS panel is the wx one (`BBL_ENABLE_AMS_CONTROL_WEB=0`) |

Bambu's proprietary network plug-in has no Haiku build; the package ships
Open Bamboo Networking instead, an open-source drop-in replacement set up
for printers on the local network (LAN-only mode with Developer Mode on,
see [Printing to a Bambu Lab printer](#printing-to-a-bambu-lab-printer)).
With it the Device tab, connecting a printer by IP address and access code,
printing and sending work as upstream. Bambu's cloud (account login,
MakerWorld, cloud sync, the user experience program) is not offered
(`GUI_App::bambu_cloud_supported()`), and nothing is ever downloaded in
place of the packaged plug-in. Should the plug-in not load, the application
falls back to the offline behaviour (`GUI_App::network_plugin_supported()`):
preparing, slicing, previewing and exporting `.gcode.3mf`/G-code work fully
offline either way, and preset updates still come from Bambu's servers.

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
tools/haiku/build-plugin.sh x86_64    # the network plug-in -> BS_SCRATCH/obn-install/x86_64/plugins
```

`build-plugin.sh` builds the commit of `tools/haiku/obn.lock` (or a checkout
named by `OBN_SRC`) against the system's OpenSSL, libcurl and zlib; mosquitto
(client library only) and cJSON come from pinned release archives and are
compiled into the plug-in, uthash is header-only. It reports Bambu Studio's
own version (`version.inc`), which the application's compatibility check
wants. `--tests` builds its unit tests instead; `tools/haiku/test/run-plugin-tests.sh`
runs them in an air/OS VM.

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
- `apps/BambuStudio/plugins/libbambu_networking.so`, `libBambuSource.so`: the
  network plug-in (about 1.9 MB on x86_64), loaded at start-up; a library of
  the same name in `~/config/settings/BambuStudio/plugins` takes precedence
  (someone's own build)
- `documentation/packages/bambustudio/open-bamboo-networking`: its README and licence
- `data/BambuStudio/resources`
- `data/mime_db/model/{3mf,stl,step,obj}`, `text/x-gcode`, `application/x-amf`
- settings and logs: `~/config/settings/BambuStudio` (the plug-in's settings:
  `obn.conf` there, written at its first start)

## Printing to a Bambu Lab printer

Over the local network, with the printer in **LAN-only mode with Developer
Mode** on (Open Bamboo Networking's "Option A"; nothing goes through Bambu's
cloud). On the printer (P1S: the nut icon, then **WLAN**; other models:
Settings > LAN Only Mode):

1. switch on **LAN Only**, then **Developer Mode** (firmware that has it:
   P1S 01.08 and later). Without Developer Mode, current firmware rejects
   every print command that is not signed with Bambu's keys ("MQTT Command
   verification failed");
2. note the printer's **IP address** and the 8-character **access code**
   shown on the same page.

In Bambu Studio:

1. select the printer's preset (e.g. *Bambu Lab P1S 0.4 nozzle*);
2. **Device** tab: the printer appears in the printer list when it is on the
   same network (SSDP); pick it and enter the access code. Otherwise use
   *Connect via LAN* / *Can't find my devices?* and enter IP address and
   access code. Status (temperatures, AMS, progress) and the camera follow;
3. slice, then **Print plate**: choose the printer and send. The `.gcode.3mf`
   goes to the printer's SD card over FTPS (port 990) and the print starts by
   MQTT (port 8883).

The plug-in keeps its settings in `~/config/settings/BambuStudio/obn.conf`
(commented; written at the first start with the package's defaults:
`block_cloud = 1`, `cloud_print = lan_only`). The printer's TLS certificate
is checked against Bambu's CA (`resources/cert/printer.cer`) and its serial
number. A printer reached through NAT or a VPN works by IP address
(`override_lan_ip = 1` for the camera and the file browser); SSDP discovery
needs the same broadcast domain.

Nothing of this runs while the Device tab is not used, apart from a UDP
listener for the printers' announcements (port 2021); a connected printer
holds one MQTT connection.

Cloud printing (a printer bound to a Bambu account, without Developer Mode)
needs Bambu's private signing credentials, which the plug-in does not
include; see the plug-in's README in
`/boot/system/documentation/packages/bambustudio/open-bamboo-networking`.

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
- Printing, without a printer: `tools/haiku/test/mock-printer.py` plays a
  P1S on the host (SSDP announcements, the TCP 3000 "detect", MQTT over TLS
  with `bblp`/access code, implicit FTPS, the port 6000 camera), and
  `tools/haiku/test/run-mock-vm.sh` boots the x86_64 test VM with the mock
  reachable as 10.0.2.100. In air/OS hrev60207+2650 (2026-11-14): the printer
  is discovered, connected with its access code and by IP address + access
  code, the Device tab shows its status, AMS and live camera, a sliced plate
  is sent (the `.gcode.3mf` arrives over FTPS, its G-code MD5 checks, the
  MQTT `project_file` names it) and the print's progress follows
  (`tools/haiku/test/check-mock-print.py`). The plug-in's own unit tests
  pass in the VM (`tools/haiku/test/run-plugin-tests.sh`, 30 of 30).
  Not tested with a real printer.

## Known limits

- Printing and the Device tab: local network only (LAN-only mode with
  Developer Mode), see above; no Bambu account, cloud printing or MakerWorld.
- The software renderer "Software Pipe" (llvmpipe from HaikuPorts' Mesa 22)
  crashes on BambuStudio's first frame; machines without a GPU driver use
  the Zink add-on's softpipe fallback, which works but is slow.
- Command-line slicing with the GUI binary renders no thumbnails (upstream
  uses a hidden GLFW window); `bambu-studio-cli` is the headless tool.
