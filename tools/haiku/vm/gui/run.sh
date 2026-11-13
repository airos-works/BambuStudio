#!/usr/bin/env bash
# Starts the deployed Bambu Studio in the GUI test VM (see deploy.sh) and
# prints the first lines of its output; the log stays in /Data/bambu/run.log.
#   tools/haiku/vm/gui/run.sh [VAR=value...] [-- application arguments]
# BS_RUN_WAIT: seconds to wait before printing the log (default 5).
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
# llvmpipe of the image (Mesa 22.0.5, LLVM 12) crashes in its generated code
# with 256 bit vectors on this host: 128 bit ones unless the caller says
# otherwise. (make-disk.sh blocks the Zink add-on, which otherwise leaves
# OpenGL with softpipe and seconds per frame.)
ENVS=(LP_NATIVE_VECTOR_WIDTH=128)
while [[ $# -gt 0 && $1 != -- ]]; do ENVS+=("$1"); shift; done
[[ ${1:-} == -- ]] && shift
ARGS=""
for arg in "$@"; do ARGS+=" $(printf '%q' "$arg")"; done
# A running instance is asked to quit first: killing a team with a hundred
# windows at once has crashed the app_server (double free in its window
# teardown, seen 2026-11-13).
"$HERE/haiku.sh" 'if ps | grep -q "[b]in/bambu-studio"; then
	hey application/x-vnd.airos-BambuStudio quit > /dev/null 2>&1 &
	for i in 1 2 3 4 5 6 7 8; do ps | grep -q "[b]in/bambu-studio" || break; sleep 1; done
	if ps | grep -q "[b]in/bambu-studio"; then echo "run.sh: instance did not quit, killing it"; kill bambu-studio; sleep 2; fi
fi'
"$HERE/haiku.sh" "cd /Data/bambu && (env ${ENVS[*]:-} ./bin/bambu-studio$ARGS > run.log 2>&1 < /dev/null &); sleep ${BS_RUN_WAIT:-5}; tail -n ${BS_RUN_LINES:-40} run.log; ps | grep -c '[b]ambu-studio' | sed 's/^/running instances: /'"
