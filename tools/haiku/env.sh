# Cross-build environment for Bambu Studio on air/OS (Haiku), sourced by the
# scripts in this directory:
#
#   . tools/haiku/env.sh x86_64|arm64
#
# Inputs (all optional):
#   HAIKU_SDK     the air/OS cross SDK of the architecture, with env.sh, the
#                 sysroot and the unwinder specs (default /data1/airos/sdk/ARCH)
#   AIROS_DEPS    the tree of extra development packages (curl, OpenSSL, the
#                 arm64 picture libraries, the arm64 GL dispatch libraries):
#                 default /data1/airos/deps/ARCH
#   BS_SCRATCH    where sources, build trees and the dependency prefix live
#                 (default /data1/scratch/bambustudio): nothing is built in
#                 the source tree
#   BS_JOBS       parallel jobs (default 16)
#
# Outputs: HAIKU_ARCH, the SDK's variables (CROSS, SYSROOT, TOOLS, TRIPLET,
# UNWIND_SPECS...), BS_ROOT (this checkout), BS_DEPS (the prefix the static
# dependencies are installed into: include/, lib/, lib/cmake/), BS_DEPS_SRC,
# BS_DEPS_BUILD, BS_BUILD, BS_TOOLCHAIN, the flag sets BS_CFLAGS, BS_CXXFLAGS
# and BS_LDFLAGS every dependency and the application are built with, and
# the function bs_gl_cmake_args (CMake's OpenGL locations).

HAIKU_ARCH=${1:-${HAIKU_ARCH:-x86_64}}
case $HAIKU_ARCH in
x86_64 | arm64) ;;
*)
	echo "env.sh: unknown architecture '$HAIKU_ARCH' (x86_64 or arm64)" >&2
	return 1 2>/dev/null || exit 1
	;;
esac
# Sourced again for the other architecture: its own defaults.
if [ -n "${BS_ENV_ARCH:-}" ] && [ "$BS_ENV_ARCH" != "$HAIKU_ARCH" ]; then
	unset HAIKU_SDK AIROS_DEPS BS_BUILD
fi
BS_ENV_ARCH=$HAIKU_ARCH
HAIKU_SDK=${HAIKU_SDK:-/data1/airos/sdk/$HAIKU_ARCH}
if [ ! -f "$HAIKU_SDK/env.sh" ]; then
	echo "env.sh: no SDK at $HAIKU_SDK" >&2
	return 1 2>/dev/null || exit 1
fi
# CROSS, TRIPLET, SYSROOT, TOOLS, UNWIND_SPECS, HAIKU_REVISION
. "$HAIKU_SDK/env.sh"
AIROS_DEPS=${AIROS_DEPS:-/data1/airos/deps/$HAIKU_ARCH}

BS_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
BS_SCRATCH=${BS_SCRATCH:-/data1/scratch/bambustudio}
BS_JOBS=${BS_JOBS:-16}
BS_DEPS=$BS_SCRATCH/deps/$HAIKU_ARCH
BS_DEPS_SRC=$BS_SCRATCH/deps-src
BS_DEPS_BUILD=$BS_SCRATCH/deps-build/$HAIKU_ARCH
BS_BUILD=${BS_BUILD:-$BS_SCRATCH/build/$HAIKU_ARCH}
BS_TOOLCHAIN=$BS_ROOT/tools/haiku/toolchain.cmake

# One package has to run on every board, so arm64 is built for the ARMv8-A
# baseline (NEON and VFPv4 are part of it) and only tuned for the
# out-of-order cores of the Raspberry Pi 4 class; x86_64 is the plain
# x86-64 baseline (SSE2). Never -march=native.
case $HAIKU_ARCH in
arm64) BS_ARCH_FLAGS="-march=armv8-a -mtune=cortex-a72" ;;
x86_64) BS_ARCH_FLAGS="-march=x86-64 -mtune=generic" ;;
esac
# Sections of their own, so that the final link (--gc-sections) drops what
# the application does not use of the static dependencies.
BS_OPT_FLAGS="-O2 -ffunction-sections -fdata-sections"
BS_CPPFLAGS="-isystem $AIROS_DEPS/boot/system/develop/headers"
BS_CFLAGS="$BS_ARCH_FLAGS $BS_OPT_FLAGS $BS_CPPFLAGS"
BS_CXXFLAGS=$BS_CFLAGS
# libgcc_s before libgcc.a (the SDK's specs): one unwinder for the whole
# process, or C++ exceptions do not cross libstdc++.
BS_LDFLAGS="-specs=$UNWIND_SPECS -L$AIROS_DEPS/boot/system/develop/lib -L$AIROS_DEPS/boot/system/lib -Wl,-rpath-link,$AIROS_DEPS/boot/system/lib -Wl,-rpath-link,$SYSROOT/boot/system/lib"

# bs_gl_cmake_args: where find_package(OpenGL) finds the system's GL. The
# x86_64 SDK has Mesa's libGL and headers in its sysroot; arm64 gets the GL
# Kit headers and the libglvnd dispatch libraries from AIROS_DEPS.
bs_gl_cmake_args() {
	local root=$SYSROOT/boot/system
	[ -e "$root/develop/lib/libGL.so" ] || root=$AIROS_DEPS/boot/system
	local lib=$root/develop/lib/libGL.so
	[ -e "$lib" ] || lib=$root/lib/libGL.so
	local include=$root/develop/headers/os/opengl
	[ -e "$include/GL/gl.h" ] || include=$root/develop/headers
	if [ ! -e "$lib" ] || [ ! -e "$include/GL/gl.h" ]; then
		echo "env.sh: no libGL or GL/gl.h for $HAIKU_ARCH in the SDK or $AIROS_DEPS" >&2
		return 1
	fi
	echo "-DOPENGL_gl_LIBRARY=$lib -DOPENGL_INCLUDE_DIR=$include -DOPENGL_GLU_INCLUDE_DIR=$include"
}
