#!/usr/bin/env bash
# stage-mime-db.sh DEST TOOLS
#
# Puts the MIME types Bambu Studio opens (src/platform/haiku/mime_db/*.rdef)
# into a package stage's data/mime_db (DEST), as Haiku's own mime_db is built:
# each rdef compiled with rc and its resources copied into attributes of an
# empty file (a directory for a supertype) with resattr. TOOLS is the SDK's
# host tool directory (rc/rc, resattr/resattr). With the types in the package,
# Tracker opens .3mf/.stl/.step/.obj/.amf/.gcode files with Bambu Studio from
# the first boot; registering them at run time works only after the
# registrar's next start for file extensions.
#
# File names: <supertype>.super.rdef, <supertype>--<subtype>.rdef.
set -euo pipefail
DEST=${1:?usage: stage-mime-db.sh DEST TOOLS}
TOOLS=${2:?usage: stage-mime-db.sh DEST TOOLS}
SRC=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../src/platform/haiku/mime_db" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$DEST"
for rdef in "$SRC"/*.rdef; do
	name=$(basename "$rdef" .rdef)
	"$TOOLS/rc/rc" -o "$work/$name.rsrc" "$rdef"
	if [[ $name == *.super ]]; then
		target=$DEST/${name%.super}
		mkdir -p "$target"
	else
		mkdir -p "$DEST/${name%%--*}"
		target=$DEST/${name%%--*}/${name#*--}
		: > "$target"
	fi
	"$TOOLS/resattr/resattr" -O -o "$target" "$work/$name.rsrc"
done
