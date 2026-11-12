#!/usr/bin/env bash
# build.sh ARCH [--gui] [target...]
#
# Cross-builds Bambu Studio for air/OS (Haiku) against the dependency prefix
# of tools/haiku/build-deps.sh:
#
#   tools/haiku/build.sh x86_64          # the headless slicer, bambu-studio-cli
#   tools/haiku/build.sh arm64
#   tools/haiku/build.sh x86_64 --gui    # the application (needs the wx port)
#
# The build tree is BS_BUILD (default BS_SCRATCH/build/ARCH, or
# build/ARCH-gui with --gui); nothing is written into the source tree.
#
# --gui takes wxWidgets from the port's install prefix: WX_PREFIX, default
# BS_SCRATCH/wx-install/ARCH (its bin/wx-config).
#
# Other inputs: see tools/haiku/env.sh. BS_CMAKE_ARGS adds cmake arguments.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ARCH=${1:?usage: build.sh x86_64|arm64 [--gui] [target...]}
shift
GUI=OFF
if [[ ${1:-} == --gui ]]; then
	GUI=ON
	shift
fi
[[ $GUI == ON && -z ${BS_BUILD:-} ]] && BS_BUILD=${BS_SCRATCH:-/data1/scratch/bambustudio}/build/$ARCH-gui
. "$HERE/env.sh" "$ARCH"

[[ -f $BS_DEPS/lib/libboost_log.a ]] \
	|| { echo "build.sh: no dependencies in $BS_DEPS (run tools/haiku/build-deps.sh $ARCH)" >&2; exit 1; }

args=()
if [[ $GUI == ON ]]; then
	WX_PREFIX=${WX_PREFIX:-$BS_SCRATCH/wx-install/$HAIKU_ARCH}
	[[ -x $WX_PREFIX/bin/wx-config ]] \
		|| { echo "build.sh: no wx-config in $WX_PREFIX/bin (the wx port's install prefix)" >&2; exit 1; }
	args+=(-DwxWidgets_CONFIG_EXECUTABLE="$WX_PREFIX/bin/wx-config")
	# shellcheck disable=SC2207
	args+=($(bs_gl_cmake_args))
fi

cmake -S "$BS_ROOT" -B "$BS_BUILD" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$BS_TOOLCHAIN" -DHAIKU_ARCH="$HAIKU_ARCH" \
	-DHAIKU_SDK="$HAIKU_SDK" -DAIROS_DEPS="$AIROS_DEPS" -DBS_DEPS="$BS_DEPS" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS="$BS_CFLAGS" -DCMAKE_CXX_FLAGS="$BS_CXXFLAGS" \
	-DCMAKE_C_FLAGS_RELEASE="-DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-DNDEBUG" \
	-DCMAKE_EXE_LINKER_FLAGS="$BS_LDFLAGS -Wl,--gc-sections" \
	-DSLIC3R_GUI=$GUI -DSLIC3R_STATIC=ON -DSLIC3R_BUILD_TESTS=OFF \
	-DSLIC3R_ENABLE_OPENMESHCRAFT_BOOLEAN=OFF -DSLIC3R_ALLOW_MCUT_BOOLEAN=ON \
	-DBBL_RELEASE_TO_PUBLIC=1 -DBBL_INTERNAL_TESTING=0 \
	"${args[@]}" ${BS_CMAKE_ARGS:-}
cmake --build "$BS_BUILD" -j"$BS_JOBS" ${1:+--target "$@"}
