#!/usr/bin/env bash
# Copies a cross-built Bambu Studio into the GUI test VM:
#   /Data/bambu/bin/bambu-studio   the executable (stripped copy)
#   /Data/bambu/resources          the resources directory (only when it changed)
#   tools/haiku/vm/gui/deploy.sh [x86_64] [--resources] [--strip]
# The executable is sent with its symbols unless --strip is given: the crash
# reports of the debug server name the functions then.
# BS_BUILD names the build tree (default BS_SCRATCH/build/ARCH-gui).
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd -- "$HERE/../../../.." && pwd)
ARCH=x86_64
FORCE=0
STRIP=0
for arg in "$@"; do
	case $arg in
	--resources) FORCE=1 ;;
	--strip) STRIP=1 ;;
	*) ARCH=$arg ;;
	esac
done
BUILD=${BS_BUILD:-${BS_SCRATCH:-/data1/scratch/bambustudio}/build/$ARCH-gui}
DIR=${BS_VM_DIR:-/data1/scratch/bambustudio/vm-gui}
VM="$HERE/haiku.sh"
BIN=$BUILD/src/bambu-studio
[[ -f $BIN ]] || { echo "deploy.sh: no $BIN" >&2; exit 1; }
. "${HAIKU_SDK:-/data1/airos/sdk/$ARCH}/env.sh"

"$VM" 'mkdir -p /Data/bambu/bin'
TMP=$BIN
if [[ $STRIP == 1 ]]; then
	TMP=$DIR/bambu-studio.stripped
	"${CROSS}strip" -o "$TMP" "$BIN"
	# strip drops the resources (signature, single launch): add them again.
	[[ -f $BUILD/src/BambuStudio.rsrc ]] && "$TOOLS/xres" -o "$TMP" "$BUILD/src/BambuStudio.rsrc"
fi
"$VM" 'cat > /Data/bambu/bin/bambu-studio.new && chmod 755 /Data/bambu/bin/bambu-studio.new && mv /Data/bambu/bin/bambu-studio.new /Data/bambu/bin/bambu-studio && mimeset -f /Data/bambu/bin/bambu-studio' < "$TMP"

# Resources: send when the tree's state (file list, sizes, times) changed.
STAMP=$(cd "$ROOT/resources" && find . -type f -printf '%p %s %T@\n' | sort | sha256sum | cut -d' ' -f1)
if [[ $FORCE == 1 || $("$VM" 'cat /Data/bambu/resources.stamp 2>/dev/null' || true) != "$STAMP" ]]; then
	echo "deploy.sh: sending resources"
	tar -C "$ROOT" -cf - resources | "$VM" 'rm -rf /Data/bambu/resources && tar -C /Data/bambu -xf -'
	"$VM" "echo $STAMP > /Data/bambu/resources.stamp"
fi
"$VM" 'ls -la /Data/bambu/bin/bambu-studio'
