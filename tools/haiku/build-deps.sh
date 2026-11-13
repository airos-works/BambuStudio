#!/usr/bin/env bash
# build-deps.sh ARCH [RECIPE...]
#
# Cross-builds the third-party libraries Bambu Studio needs on air/OS (Haiku)
# into one static, trimmed prefix (BS_DEPS, see tools/haiku/env.sh):
#
#   tools/haiku/build-deps.sh x86_64          # everything, in dependency order
#   tools/haiku/build-deps.sh arm64 occt      # one recipe
#   FORCE=1 tools/haiku/build-deps.sh x86_64 boost
#   tools/haiku/build-deps.sh x86_64 --list   # the recipes and their versions
#
# What is NOT built here, on purpose:
#   - zlib, libpng, libjpeg, libtiff, expat, FreeType, curl, OpenSSL, ICU,
#     FFmpeg: air/OS ships them; the SDK sysroot and AIROS_DEPS have their
#     development files.
#   - OpenCV, GLFW, OpenVDB/Blosc/OpenEXR, fontconfig, GTK, DBus, GStreamer,
#     hidapi/libudev: not dependencies on air/OS (see the top-level
#     CMakeLists.txt and src/platform/haiku).
#   - wxWidgets: its own port (wx-install/ARCH), built by the wx tooling.
#
# Every source is a pinned release archive checked against its SHA-256; the
# versions are the ones deps/*/*.cmake pin for the other platforms unless a
# recipe says why not. Sources are unpacked once into BS_DEPS_SRC and shared
# by the architectures; every build is out of tree in BS_DEPS_BUILD/NAME.
# Patches: the ones deps/ applies on the other platforms, then
# tools/haiku/patches/NAME/*.patch.
#
# A recipe is skipped when its inputs (the recipe, its patches, the flags and
# the SDK revision) are unchanged since its last successful build.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ARCH=${1:?usage: build-deps.sh x86_64|arm64 [recipe...]}
shift
. "$HERE/env.sh" "$ARCH"

ORDER=(boost tbb gmp mpfr cgal cereal nlopt libnoise occt assimp glew libharu)

PATCHES=$HERE/patches
DOWNLOADS=$BS_DEPS_SRC/downloads
BUILD_TRIPLET=$(uname -m)-pc-linux-gnu
mkdir -p "$DOWNLOADS" "$BS_DEPS_BUILD" "$BS_DEPS"/{include,lib}

CC="${CROSS}gcc --sysroot=$SYSROOT"
CXX="${CROSS}g++ --sysroot=$SYSROOT"

die() {
	echo "build-deps: $*" >&2
	exit 1
}
note() { echo "== $* ==" >&2; }

# fetch URL SHA256 [FILE]: the cached, verified download; prints its path.
fetch() {
	local url=$1 sha=$2 file=$DOWNLOADS/${3:-$(basename "$1")}
	if [[ ! -f $file ]] || ! echo "$sha  $file" | sha256sum -c --quiet 2>/dev/null; then
		curl -fL --retry 3 -sS -o "$file.part" "$url" || die "cannot download $url"
		echo "$sha  $file.part" | sha256sum -c --quiet || die "$url: checksum mismatch"
		mv "$file.part" "$file"
	fi
	echo "$file"
}

# unpack ARCHIVE DIR [PATCH...]: BS_DEPS_SRC/DIR from the archive (its single
# top directory stripped, the archive's file times kept) with the patches
# applied (-p1), once: the tree is redone when the archive or a patch changes.
unpack() {
	local archive=$1 dir=$BS_DEPS_SRC/$2 stamp patch tmp
	shift 2
	stamp=$(cat "$archive" "$@" /dev/null | sha256sum | cut -d' ' -f1)
	SRC=$dir
	[[ -f $dir/.bs-source && $(cat "$dir/.bs-source") == "$stamp" ]] && return 0
	rm -rf "$dir" "$dir.tmp"
	mkdir -p "$dir.tmp"
	case $archive in
	*.zip) (cd "$dir.tmp" && unzip -q "$archive") ;;
	*) tar xf "$archive" -C "$dir.tmp" ;;
	esac
	tmp=("$dir.tmp"/*)
	[[ ${#tmp[@]} == 1 && -d ${tmp[0]} ]] || die "$archive: not a single top directory"
	mv "${tmp[0]}" "$dir"
	rmdir "$dir.tmp"
	touch "$dir/.bs-unpacked"
	local newest=
	for patch in "$@"; do
		patch -d "$dir" -p1 -s -N --no-backup-if-mismatch < "$patch" \
			|| die "$(basename "$patch") does not apply to $(basename "$dir")"
		[[ -n $newest && ! $patch -nt $newest ]] || newest=$patch
	done
	# A patched file is as old as the newest patch, not as this extraction:
	# unpacking again for an unrelated reason must not make every user of an
	# installed header rebuild.
	[[ -z $newest ]] || find "$dir" -type f -newer "$dir/.bs-unpacked" -exec touch -r "$newest" {} +
	rm -f "$dir/.bs-unpacked"
	echo "$stamp" > "$dir/.bs-source"
}

# haiku_patches NAME: the Haiku patches of a recipe, in order.
haiku_patches() {
	local p
	for p in "$PATCHES/$1"/*.patch; do
		[[ -f $p ]] && echo "$p"
	done
	return 0
}

# cmake_dep SOURCE [cmake args]: configure, build and install into BS_DEPS.
cmake_dep() {
	local source=$1 home
	shift
	# A build tree configured from another checkout (the in-tree GLEW and
	# libharu of a CI snapshot): CMake refuses it, so start afresh.
	home=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt" 2>/dev/null || true)
	[[ -z $home || $home == "$(cd "$source" && pwd)" ]] || rm -rf "$BUILD"
	cmake -S "$source" -B "$BUILD" -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE="$BS_TOOLCHAIN" -DHAIKU_ARCH="$HAIKU_ARCH" \
		-DHAIKU_SDK="$HAIKU_SDK" -DAIROS_DEPS="$AIROS_DEPS" -DBS_DEPS="$BS_DEPS" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$BS_DEPS" \
		-DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INSTALL_INCLUDEDIR=include \
		-DCMAKE_C_FLAGS="$BS_CFLAGS" -DCMAKE_CXX_FLAGS="$BS_CXXFLAGS" \
		-DCMAKE_C_FLAGS_RELEASE="-DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-DNDEBUG" \
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_SHARED_LIBS=OFF "$@" > "$BUILD.configure.log" 2>&1 \
		|| die "$NAME: configure failed, see $BUILD.configure.log"
	cmake --build "$BUILD" -j"$BS_JOBS" > "$BUILD.build.log" 2>&1 \
		|| die "$NAME: build failed, see $BUILD.build.log"
	cmake --install "$BUILD" > "$BUILD.install.log" 2>&1 \
		|| die "$NAME: install failed, see $BUILD.install.log"
}

# autotools_dep SOURCE [configure args]: the same for a configure script.
autotools_dep() {
	local source=$1
	shift
	rm -rf "$BUILD"
	mkdir -p "$BUILD"
	(
		cd "$BUILD"
		"$source/configure" --host="$TRIPLET" --build="$BUILD_TRIPLET" --prefix="$BS_DEPS" \
			--enable-static --disable-shared \
			CC="$CC" CXX="$CXX" CFLAGS="$BS_CFLAGS" CXXFLAGS="$BS_CXXFLAGS" \
			AR="${CROSS}ar" RANLIB="${CROSS}ranlib" NM="${CROSS}nm" STRIP="${CROSS}strip" \
			"$@" > "$BUILD.configure.log" 2>&1 \
			|| die "$NAME: configure failed, see $BUILD.configure.log"
		make -j"$BS_JOBS" > "$BUILD.build.log" 2>&1 \
			|| die "$NAME: build failed, see $BUILD.build.log"
		make install > "$BUILD.install.log" 2>&1 \
			|| die "$NAME: install failed, see $BUILD.install.log"
	)
}

# --- recipes ----------------------------------------------------------------
# Each sets VERSION and defines build_NAME; "$SRC" is the unpacked source.

# Boost: only the libraries the tree uses (BOOST_INCLUDE_LIBRARIES pulls in
# what they depend on); the compiled ones are the components of the
# top-level CMakeLists.txt (_boost_components). Boost.Locale converts through
# the system's ICU (every air/OS installation has it: the Locale Kit is built
# on it), so no iconv is needed and boost::locale::normalize() works.
VERSION_boost=1.84.0
build_boost() {
	local archive libs
	archive=$(fetch https://github.com/boostorg/boost/releases/download/boost-$VERSION/boost-$VERSION.tar.gz \
		4d27e9efed0f6f152dc28db6430b9d3dfb40c0345da7342eaa5a987dde57bd95)
	unpack "$archive" boost-$VERSION \
		"$BS_ROOT/deps/Boost/0001-FIX-OBS-cannot-start-streaming-on-MAC.patch" $(haiku_patches boost)
	# compiled: the application's components
	libs="system;filesystem;thread;log;locale;regex;chrono;atomic;date_time;iostreams"
	# header-only, used by libslic3r, the GUI and CGAL
	libs+=";algorithm;any;asio;assign;beast;bimap;bind;circular_buffer;container;container_hash"
	libs+=";conversion;crc;dll;dynamic_bitset;endian;foreach;format;functional;fusion;geometry"
	libs+=";graph;heap;icl;integer;interprocess;intrusive;iterator;lexical_cast;math;mpl"
	libs+=";multi_array;multi_index;multiprecision;nowide;numeric/conversion;numeric/interval"
	libs+=";optional;parameter;phoenix;polygon;pool;predef;preprocessor;process;program_options"
	libs+=";property_map;property_tree;random;range;rational;serialization;smart_ptr;spirit"
	libs+=";stacktrace;static_assert;tokenizer;tuple;type_traits;typeof;unordered;utility;uuid"
	libs+=";variant;variant2"
	cmake_dep "$SRC" -DBOOST_INCLUDE_LIBRARIES="$libs" -DBUILD_TESTING=OFF \
		-DBOOST_LOCALE_ENABLE_ICU=ON -DBOOST_LOCALE_ENABLE_ICONV=OFF \
		-DBOOST_LOCALE_ENABLE_STD=OFF -DBOOST_LOCALE_ENABLE_POSIX=ON \
		-DBOOST_IOSTREAMS_ENABLE_BZIP2=OFF -DBOOST_IOSTREAMS_ENABLE_LZMA=OFF \
		-DBOOST_IOSTREAMS_ENABLE_ZSTD=OFF -DBOOST_IOSTREAMS_ENABLE_ZLIB=ON \
		-DBOOST_STACKTRACE_ENABLE_BACKTRACE=OFF -DBOOST_STACKTRACE_ENABLE_ADDR2LINE=OFF \
		-DBOOST_STACKTRACE_ENABLE_BASIC=OFF -DBOOST_STACKTRACE_ENABLE_NOOP=ON \
		-DBOOST_INSTALL_LAYOUT=system
}

# oneTBB, static (tbb and tbbmalloc). Its worker count comes from
# sysconf(_SC_NPROCESSORS_ONLN), which Haiku answers from get_system_info().
VERSION_tbb=2021.5.0
build_tbb() {
	local archive
	archive=$(fetch https://github.com/oneapi-src/oneTBB/archive/refs/tags/v$VERSION.zip \
		83ea786c964a384dd72534f9854b419716f412f9d43c0be88d41874763e7bb47 oneTBB-$VERSION.zip)
	unpack "$archive" oneTBB-$VERSION $(haiku_patches tbb)
	# -D__unix__: what oneTBB's sources ask before they use POSIX, and Haiku's
	# compiler does not predefine (the patch covers the installed headers).
	cmake_dep "$SRC" -DTBB_BUILD_SHARED=OFF -DTBB_BUILD_TESTS=OFF -DTBB_TEST=OFF \
		-DTBB_STRICT=OFF -DTBBMALLOC_PROXY_BUILD=OFF -DTBB_DISABLE_HWLOC_AUTOMATIC_SEARCH=ON \
		-DCMAKE_CXX_FLAGS="$BS_CXXFLAGS -D__unix__=1"
}

# GMP and MPFR for CGAL's exact arithmetic.
VERSION_gmp=6.2.1
build_gmp() {
	local archive
	archive=$(fetch https://github.com/bambulab/gmp/archive/refs/tags/$VERSION.tar.gz \
		705ae57ee2014b2c6fc0f572c85ee43276b99b6b256ee16c1a9d3a8c4e3609d5 gmp-$VERSION.tar.gz)
	unpack "$archive" gmp-$VERSION "$BS_ROOT/deps/GMP/0001-GMP_GCC15.patch" \
		"$BS_ROOT/deps/GMP/0002-GMP_arm64_avoid_x18_reserved_on_darwin.patch"
	# this release's config.sub predates the Haiku triplets
	cp /usr/share/misc/config.sub "$SRC/configfsf.sub"
	cp /usr/share/misc/config.guess "$SRC/configfsf.guess"
	autotools_dep "$SRC" --enable-cxx --with-pic
}

VERSION_mpfr=4.2.1
build_mpfr() {
	local archive
	# deps/MPFR pins 3.1.6 (2017), whose configure does not know aarch64
	# Haiku; 4.2.1 is what the OrcaSlicer port builds CGAL 5.4 with.
	archive=$(fetch https://www.mpfr.org/mpfr-$VERSION/mpfr-$VERSION.tar.bz2 \
		b9df93635b20e4089c29623b19420c4ac848a1b29df1cfd59f26cab0d2666aa0)
	unpack "$archive" mpfr-$VERSION
	autotools_dep "$SRC" --with-gmp="$BS_DEPS" --with-pic
}

# CGAL is header-only: the build installs the headers and the CMake package.
VERSION_cgal=5.4
build_cgal() {
	local archive
	archive=$(fetch https://github.com/CGAL/cgal/archive/refs/tags/v$VERSION.zip \
		d7605e0a5a5ca17da7547592f6f6e4a59430a0bc861948974254d0de43eab4c0 cgal-$VERSION.zip)
	unpack "$archive" cgal-$VERSION "$BS_ROOT/deps/CGAL/0001-clang19.patch"
	cmake_dep "$SRC" -DCGAL_INSTALL_CMAKE_DIR=lib/cmake/CGAL
}

VERSION_cereal=1.3.0
build_cereal() {
	local archive
	archive=$(fetch https://github.com/USCiLab/cereal/archive/refs/tags/v$VERSION.zip \
		71642cb54658e98c8f07a0f0d08bf9766f1c3771496936f6014169d3726d9657 cereal-$VERSION.zip)
	unpack "$archive" cereal-$VERSION
	cmake_dep "$SRC" -DJUST_INSTALL_CEREAL=ON -DSKIP_PERFORMANCE_COMPARISON=ON -DBUILD_TESTS=OFF
}

VERSION_nlopt=2.5.0
build_nlopt() {
	local archive
	archive=$(fetch https://github.com/stevengj/nlopt/archive/v$VERSION.tar.gz \
		c6dd7a5701fff8ad5ebb45a3dc8e757e61d52658de3918e38bab233e7fd3b4ae nlopt-$VERSION.tar.gz)
	unpack "$archive" nlopt-$VERSION
	cmake_dep "$SRC" -DNLOPT_PYTHON=OFF -DNLOPT_OCTAVE=OFF -DNLOPT_MATLAB=OFF \
		-DNLOPT_GUILE=OFF -DNLOPT_SWIG=OFF -DNLOPT_TESTS=OFF
}

VERSION_libnoise=1.0.0
build_libnoise() {
	local archive
	archive=$(fetch https://github.com/bambulab/libnoise/archive/refs/tags/v$VERSION.zip \
		7aae3433e4c841a1b15ddf40dc995b3d32efb154b92b6796888002e275bff859 libnoise-$VERSION.zip)
	unpack "$archive" libnoise-$VERSION
	cmake_dep "$SRC"
}

# Open CASCADE for STEP import and the text shapes: only the toolkits
# src/libslic3r/CMakeLists.txt links (OCCT_LIBS) and what they depend on, as
# static libraries; no Draw, no visualisation drivers, no X11, no fontconfig
# (Font_FontMgr scans the system's font directories on Haiku).
VERSION_occt=7.6.0
build_occt() {
	local archive
	archive=$(fetch https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/V7_6_0.zip \
		28334f0e98f1b1629799783e9b4d21e05349d89e695809d7e6dfa45ea43e1dbc occt-$VERSION.zip)
	unpack "$archive" occt-$VERSION "$BS_ROOT/deps/OCCT/0001-OCCT-fix.patch" $(haiku_patches occt)
	cmake_dep "$SRC" -DBUILD_LIBRARY_TYPE=Static -DINSTALL_DIR_LAYOUT=Windows \
		-DUSE_TK=OFF -DUSE_TBB=OFF -DUSE_FREETYPE=ON -DUSE_FFMPEG=OFF -DUSE_VTK=OFF \
		-DUSE_FREEIMAGE=OFF -DUSE_OPENVR=OFF -DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF \
		-DUSE_OPENGL=OFF -DUSE_GLES2=OFF -DUSE_XLIB=OFF \
		-DBUILD_DOC_Overview=OFF -DBUILD_MODULE_ApplicationFramework=OFF \
		-DBUILD_MODULE_DataExchange=OFF -DBUILD_MODULE_Draw=OFF \
		-DBUILD_MODULE_FoundationClasses=OFF -DBUILD_MODULE_ModelingAlgorithms=OFF \
		-DBUILD_MODULE_ModelingData=OFF -DBUILD_MODULE_Visualization=OFF \
		-DBUILD_ADDITIONAL_TOOLKITS="TKXDESTEP;TKSTEP;TKSTEP209;TKSTEPAttr;TKSTEPBase;TKXCAF;TKXSBase;TKVCAF;TKCAF;TKLCAF;TKCDF;TKV3d;TKService;TKMesh;TKBO;TKPrim;TKHLR;TKShHealing;TKTopAlgo;TKGeomAlgo;TKBRep;TKGeomBase;TKG3d;TKG2d;TKMath;TKernel"
}

# Assimp reads the textured formats libslic3r hands it (FBX, glTF/GLB,
# textured OBJ): those three importers, no exporters, the system's zlib.
VERSION_assimp=5.4.3
build_assimp() {
	local archive
	archive=$(fetch https://github.com/assimp/assimp/archive/refs/tags/v$VERSION.tar.gz \
		66dfbaee288f2bc43172440a55d0235dfc7bf885dda6435c038e8000e79582cb assimp-$VERSION.tar.gz)
	unpack "$archive" assimp-$VERSION \
		"$BS_ROOT/deps/Assimp/0001-FBX-fix-negative-TypedIndex-OOB.patch" \
		"$BS_ROOT/deps/Assimp/0002-glTF1-fix-index-accessor-type-confusion-OOB.patch"
	cmake_dep "$SRC" -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_SAMPLES=OFF \
		-DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_NO_EXPORT=ON \
		-DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF -DASSIMP_BUILD_GLTF_IMPORTER=ON \
		-DASSIMP_BUILD_OBJ_IMPORTER=ON -DASSIMP_BUILD_FBX_IMPORTER=ON \
		-DASSIMP_BUILD_ZLIB=OFF -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_INSTALL_PDB=OFF \
		-DASSIMP_BUILD_DRACO=OFF -DASSIMP_INJECT_DEBUG_POSTFIX=OFF
}

# GLEW (the tree's own copy, deps/GLEW/glew) against the system's OpenGL.
VERSION_glew=$(cat "$BS_ROOT/deps/GLEW/glew/VERSION" 2>/dev/null || echo bundled)
build_glew() {
	SRC=$BS_ROOT/deps/GLEW/glew
	cmake_dep "$SRC" $(bs_gl_cmake_args)
}

# libharu writes the PDF of the assembly steps (GUI only); the tree's copy.
VERSION_libharu=bundled
build_libharu() {
	SRC=$BS_ROOT/deps/libharu/libharu
	cmake_dep "$SRC" -DLIBHPDF_EXAMPLES=OFF
}

# --- driver -----------------------------------------------------------------
if [[ ${1:-} == --list ]]; then
	for NAME in "${ORDER[@]}"; do
		version=VERSION_$NAME
		echo "$NAME ${!version}"
	done
	exit 0
fi

RECIPES=("${@:-${ORDER[@]}}")
for NAME in "${RECIPES[@]}"; do
	declare -F "build_$NAME" > /dev/null || die "no recipe '$NAME' (have: ${ORDER[*]})"
	version=VERSION_$NAME
	VERSION=${!version}
	BUILD=$BS_DEPS_BUILD/$NAME
	stamp=$(
		{
			declare -f "build_$NAME" cmake_dep autotools_dep bs_gl_cmake_args
			echo "$VERSION $BS_CFLAGS $BS_LDFLAGS ${HAIKU_REVISION:-}"
			cat "$BS_TOOLCHAIN" "$HERE/haiku-rules.cmake" $(haiku_patches "$NAME")
		} | sha256sum | cut -d' ' -f1
	)
	if [[ ${FORCE:-0} != 1 && -f $BUILD.stamp && $(cat "$BUILD.stamp") == "$stamp" ]]; then
		note "$NAME $VERSION ($HAIKU_ARCH): up to date"
		continue
	fi
	note "$NAME $VERSION ($HAIKU_ARCH)"
	rm -f "$BUILD.stamp"
	"build_$NAME"
	echo "$stamp" > "$BUILD.stamp"
done
note "dependencies for $HAIKU_ARCH are in $BS_DEPS"
