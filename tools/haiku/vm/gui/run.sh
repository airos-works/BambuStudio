#!/usr/bin/env bash
# Starts the deployed Bambu Studio in the GUI test VM (see deploy.sh) and
# prints the first lines of its output; the log stays in /Data/bambu/run.log.
#   tools/haiku/vm/gui/run.sh [VAR=value...] [-- application arguments]
# BS_RUN_WAIT: seconds to wait before printing the log (default 5).
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ENVS=()
while [[ $# -gt 0 && $1 != -- ]]; do ENVS+=("$1"); shift; done
[[ ${1:-} == -- ]] && shift
ARGS=""
for arg in "$@"; do ARGS+=" $(printf '%q' "$arg")"; done
"$HERE/haiku.sh" "kill -9 bambu-studio 2>/dev/null; cd /Data/bambu && (env ${ENVS[*]:-} ./bin/bambu-studio$ARGS > run.log 2>&1 &); sleep ${BS_RUN_WAIT:-5}; tail -n ${BS_RUN_LINES:-40} run.log; ps | grep -c '[b]ambu-studio' | sed 's/^/running instances: /'"
