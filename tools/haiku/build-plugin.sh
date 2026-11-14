#!/usr/bin/env bash
# build-plugin.sh ARCH [--tests]
#
# Builds the network plug-in the air/OS package ships: Open Bamboo Networking
# (an open-source replacement for Bambu Lab's proprietary libbambu_networking,
# same C ABI), at the commit pinned in tools/haiku/obn.lock:
#
#   tools/haiku/build-plugin.sh x86_64          # -> BS_SCRATCH/obn-install/x86_64
#   tools/haiku/build-plugin.sh arm64
#   tools/haiku/build-plugin.sh x86_64 --tests  # also its unit tests (build tree
#                                               # BS_SCRATCH/obn-build/x86_64-tests)
#
# Output: OBN_PREFIX (default BS_SCRATCH/obn-install/ARCH) gets
#   plugins/libbambu_networking.so   the plug-in Bambu Studio loads
#   plugins/libBambuSource.so        its camera/file-browser companion
#   LICENSE, README.md               (AGPL-3.0, ClusterM's project)
# both stripped. Bambu Studio finds plugins/ beside its executable.
#
# Dependencies: OpenSSL, libcurl and zlib of the system (the SDK and
# AIROS_DEPS); mosquitto (client library only) and cJSON are compiled into
# the plug-in from the pinned release archives below, uthash is header-only.
# Nothing is fetched by CMake (FETCHCONTENT_FULLY_DISCONNECTED).
#
# Inputs: see tools/haiku/env.sh (HAIKU_SDK, AIROS_DEPS, BS_SCRATCH, BS_JOBS);
# OBN_SRC overrides the plug-in source (a local checkout is used as it is).
# The plug-in reports Bambu Studio's own version (version.inc): Bambu Studio
# loads a plug-in only when the first three components match.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ARCH=${1:?usage: build-plugin.sh x86_64|arm64 [--tests]}
TESTS=OFF
[[ ${2:-} == --tests ]] && TESTS=ON
. "$HERE/env.sh" "$ARCH"

die() {
	echo "build-plugin: $*" >&2
	exit 1
}

# Third-party sources: pinned release archives and their SHA-256. mosquitto
# and cJSON are the versions the plug-in's cmake/Vendor*.cmake pins.
MOSQUITTO_URL=https://mosquitto.org/files/source/mosquitto-2.1.2.tar.gz
MOSQUITTO_SHA=fd905380691ac65ea5a93779e8214941829e3d6e038d5edff9eac5fd74cbed02
CJSON_URL=https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz
CJSON_SHA=3aa806844a03442c00769b83e99970be70fbef03735ff898f4811dd03b9f5ee5
UTHASH_URL=https://github.com/troydhanson/uthash/archive/refs/tags/v2.3.0.tar.gz
UTHASH_SHA=e10382ab75518bad8319eb922ad04f907cb20cccb451a3aa980c9d005e661acc

OBN_ROOT=$BS_SCRATCH/obn
DOWNLOADS=$OBN_ROOT/downloads
BUILD=$BS_SCRATCH/obn-build/$HAIKU_ARCH
[[ $TESTS == ON ]] && BUILD=$BUILD-tests
OBN_PREFIX=${OBN_PREFIX:-$BS_SCRATCH/obn-install/$HAIKU_ARCH}
mkdir -p "$DOWNLOADS" "$BUILD"

# fetch URL SHA256 FILE: the cached, verified download; prints its path.
fetch() {
	local url=$1 sha=$2 file=$DOWNLOADS/$3
	if [[ ! -f $file ]] || ! echo "$sha  $file" | sha256sum -c --quiet 2>/dev/null; then
		curl -fL --retry 3 -sS -o "$file.part" "$url" || die "cannot download $url"
		echo "$sha  $file.part" | sha256sum -c --quiet || die "$url: checksum mismatch"
		mv "$file.part" "$file"
	fi
	echo "$file"
}

# unpack ARCHIVE DIR: DIR from the archive (single top directory stripped),
# redone when the archive changes. Per build tree: the plug-in's CMake
# patches the mosquitto sources in place.
unpack() {
	local archive=$1 dir=$2 stamp tmp
	stamp=$(sha256sum < "$archive" | cut -d' ' -f1)
	[[ -f $dir/.obn-source && $(cat "$dir/.obn-source") == "$stamp" ]] && return 0
	rm -rf "$dir" "$dir.tmp"
	mkdir -p "$dir.tmp"
	tar xf "$archive" -C "$dir.tmp"
	tmp=("$dir.tmp"/*)
	[[ ${#tmp[@]} == 1 && -d ${tmp[0]} ]] || die "$archive: not a single top directory"
	mv "${tmp[0]}" "$dir"
	rmdir "$dir.tmp"
	echo "$stamp" > "$dir/.obn-source"
}

# The plug-in source: the pinned commit (or OBN_SRC as it is).
read -r _ repo < <(grep '^repository ' "$HERE/obn.lock")
read -r _ commit < <(grep '^commit ' "$HERE/obn.lock")
if [[ -n ${OBN_SRC:-} ]]; then
	src=$OBN_SRC
	commit=$(git -C "$src" rev-parse HEAD 2>/dev/null || echo unknown)
else
	src=$OBN_ROOT/src
	[[ -d $src/.git ]] || git clone -q --no-checkout "$repo" "$src"
	git -C "$src" cat-file -e "$commit^{commit}" 2>/dev/null || git -C "$src" fetch -q origin "$commit"
	git -C "$src" checkout -q --detach --force "$commit"
fi

deps=$BUILD/_sources
unpack "$(fetch "$MOSQUITTO_URL" "$MOSQUITTO_SHA" mosquitto-2.1.2.tar.gz)" "$deps/mosquitto"
unpack "$(fetch "$CJSON_URL" "$CJSON_SHA" cJSON-1.7.18.tar.gz)" "$deps/cjson"
unpack "$(fetch "$UTHASH_URL" "$UTHASH_SHA" uthash-2.3.0.tar.gz)" "$deps/uthash"

# OBN_VERSION: Bambu Studio's version (its gate compares MM.mm.pp).
version=$(sed -n 's/^set(SLIC3R_VERSION "\([0-9.]*\)")/\1/p' "$BS_ROOT/version.inc")
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "cannot read SLIC3R_VERSION from version.inc"

# The obn.conf the plug-in writes on its first start (in Bambu Studio's
# settings folder): its own defaults already suit a printer in LAN-only mode
# with Developer Mode on (block_cloud = 1); prints never take the cloud path
# either, even where someone unblocks the cloud.
CONF_DEFAULTS="cloud_print=lan_only"
# The unit tests check the plug-in's own defaults.
[[ $TESTS == ON ]] && CONF_DEFAULTS=

# Check projects CMake runs without the toolchain variables (mosquitto's
# CheckIPOSupported) read them from the environment.
export HAIKU_ARCH HAIKU_SDK AIROS_DEPS BS_DEPS

args=(-G Ninja
	-DCMAKE_TOOLCHAIN_FILE="$BS_TOOLCHAIN" -DHAIKU_ARCH="$HAIKU_ARCH"
	-DHAIKU_SDK="$HAIKU_SDK" -DAIROS_DEPS="$AIROS_DEPS" -DBS_DEPS="$BS_DEPS"
	-DCMAKE_BUILD_TYPE=Release
	-DCMAKE_C_FLAGS="$BS_CFLAGS -isystem $deps/uthash/src"
	-DCMAKE_CXX_FLAGS="$BS_CXXFLAGS -isystem $deps/uthash/src"
	-DCMAKE_C_FLAGS_RELEASE="-DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-DNDEBUG"
	-DCMAKE_SHARED_LINKER_FLAGS="$BS_LDFLAGS -Wl,--gc-sections"
	-DCMAKE_EXE_LINKER_FLAGS="$BS_LDFLAGS"
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5
	-DFETCHCONTENT_FULLY_DISCONNECTED=ON
	-DFETCHCONTENT_SOURCE_DIR_ECLIPSE_MOSQUITTO="$deps/mosquitto"
	-DFETCHCONTENT_SOURCE_DIR_CJSON="$deps/cjson"
	-DWITH_LTO=OFF
	-DOBN_VERSION="$version" -DOBN_CLIENT_TYPE=bambu_studio -DOBN_RELEASE=ON
	-DOBN_PATCH_CLIENT_CONF=OFF -DOBN_BUILD_TESTS=$TESTS
	-DOBN_CONF_DEFAULTS="$CONF_DEFAULTS"
	-DCMAKE_INSTALL_PREFIX="$BUILD/_install")

# A build tree configured from another source directory: start afresh.
home=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt" 2>/dev/null || true)
[[ -z $home || $home == "$(cd "$src" && pwd)" ]] || rm -rf "$BUILD/CMakeCache.txt" "$BUILD/CMakeFiles"
settings=$(printf '%s\n' "${args[@]}" | sha256sum | cut -d' ' -f1)
if [[ ! -f $BUILD/build.ninja || $(cat "$BUILD/.obn-settings" 2>/dev/null) != "$settings" ]]; then
	cmake -S "$src" -B "$BUILD" "${args[@]}"
	echo "$settings" > "$BUILD/.obn-settings"
fi
cmake --build "$BUILD" -j"$BS_JOBS"

[[ $TESTS == ON ]] && exit 0

# Install: the two libraries the application loads, stripped.
rm -rf "$OBN_PREFIX.tmp"
mkdir -p "$OBN_PREFIX.tmp/plugins"
"${CROSS}strip" --strip-unneeded -o "$OBN_PREFIX.tmp/plugins/libbambu_networking.so" "$BUILD/libbambu_networking.so"
"${CROSS}strip" --strip-unneeded -o "$OBN_PREFIX.tmp/plugins/libBambuSource.so" "$BUILD/libBambuSource.so"
cp "$src/LICENSE" "$src/README.md" "$OBN_PREFIX.tmp/"
echo "$commit" > "$OBN_PREFIX.tmp/.obn-commit"
rm -rf "$OBN_PREFIX"
mv "$OBN_PREFIX.tmp" "$OBN_PREFIX"
echo "build-plugin: $OBN_PREFIX/plugins (Open Bamboo Networking $commit, version $version)"
