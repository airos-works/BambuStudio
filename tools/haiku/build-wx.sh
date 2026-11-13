#!/usr/bin/env bash
# build-wx.sh ARCH
#
# Fetches the wxWidgets commit pinned in tools/haiku/wx.lock into
# BS_SCRATCH/wx-src and builds it with the port's own build/haiku/build.sh:
# build tree BS_SCRATCH/wx-pinned-build/ARCH, install prefix
# BS_SCRATCH/wx-pinned/ARCH (pass it to build.sh --gui as WX_PREFIX; build.sh's
# default, BS_SCRATCH/wx-install, is the prefix of a wx development tree).
# Nothing is rebuilt when the prefix already holds that commit.
#
# Inputs: see tools/haiku/env.sh (HAIKU_SDK, AIROS_DEPS, BS_SCRATCH, BS_JOBS);
# WX_SRC overrides the source directory (a local checkout is used as it is).
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ARCH=${1:?usage: build-wx.sh x86_64|arm64}
. "$HERE/env.sh" "$ARCH"

read -r _ repo < <(grep '^repository ' "$HERE/wx.lock")
read -r _ commit < <(grep '^commit ' "$HERE/wx.lock")
prefix=$BS_SCRATCH/wx-pinned/$HAIKU_ARCH
stamp=$prefix/.wx-commit

if [[ -n ${WX_SRC:-} ]]; then
	src=$WX_SRC
	commit=$(git -C "$src" rev-parse HEAD)
else
	src=$BS_SCRATCH/wx-src
	if [[ -f $stamp && $(cat "$stamp") == "$commit" && -x $prefix/bin/wx-config ]]; then
		echo "wxWidgets $commit already in $prefix"
		exit 0
	fi
	[[ -d $src/.git ]] || git clone -q --no-checkout "$repo" "$src"
	git -C "$src" cat-file -e "$commit^{commit}" 2>/dev/null || git -C "$src" fetch -q origin "$commit"
	git -C "$src" checkout -q --detach --force "$commit"
fi

# HAIKU_SDK is .../sdk/ARCH and AIROS_DEPS .../deps/ARCH: the port's script
# takes their parents.
WX_BUILD_ROOT=$BS_SCRATCH/wx-pinned-build WX_INSTALL_ROOT=$BS_SCRATCH/wx-pinned \
	AIROS_SDK=$(dirname "$HAIKU_SDK") AIROS_DEPS=$(dirname "$AIROS_DEPS") \
	WX_JOBS=$BS_JOBS "$src/build/haiku/build.sh" "$HAIKU_ARCH"
echo "$commit" > "$stamp"
