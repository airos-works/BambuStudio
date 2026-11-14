#!/usr/bin/env bash
# Runs the network plug-in's unit tests (Open Bamboo Networking's ctest
# suite) in the air/OS x86_64 test VM:
#
#   tools/haiku/build-plugin.sh x86_64 --tests     # builds them
#   tools/haiku/test/run-mock-vm.sh                # or any VM of tools/haiku/vm/gui
#   tools/haiku/test/run-plugin-tests.sh [x86_64]
#
# The test programs, the plug-in and the fixtures are copied to
# /Data/obn-tests in the guest; every test runs from its ctest working
# directory with a 120 s limit. lan_live skips itself without a printer
# (OBN_LIVE_IP unset). Prints one line per test and a summary; exits 1 when
# a test fails. Uses BS_VM_DIR/BS_VM_SSH like the VM scripts (defaults of
# run-mock-vm.sh).
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ARCH=${1:-x86_64}
export BS_VM_DIR=${BS_VM_DIR:-/data1/scratch/bambustudio/vm-obn} BS_VM_SSH=${BS_VM_SSH:-2249}
BUILD=${BS_SCRATCH:-/data1/scratch/bambustudio}/obn-build/$ARCH-tests
VM="$HERE/../vm/gui/haiku.sh"
[[ -f $BUILD/CTestTestfile.cmake ]] || { echo "run-plugin-tests: no test build in $BUILD" >&2; exit 1; }
SRC=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD/CMakeCache.txt")

# name, program, arguments and working directory of every test, with the
# host paths of the build tree and the source tree turned into guest paths.
list=$(cd "$BUILD" && ctest --show-only=json-v1 | python3 -c '
import json, sys, shlex
build, src = sys.argv[1], sys.argv[2]
def guest(p):
    return p.replace(build, "/Data/obn-tests/build").replace(src, "/Data/obn-tests/src")
for t in json.load(sys.stdin)["tests"]:
    wd = [p["value"] for p in t.get("properties", []) if p["name"] == "WORKING_DIRECTORY"]
    print(t["name"], shlex.quote(guest(wd[0] if wd else build)), " ".join(shlex.quote(guest(c)) for c in t["command"]))
' "$BUILD" "$SRC")

programs=$(cd "$BUILD" && find . -maxdepth 1 -type f -perm -u+x -name '*test*' -o -maxdepth 1 -name 'probe_plugin' -o -maxdepth 1 -name '*.so')
"$VM" 'rm -rf /Data/obn-tests && mkdir -p /Data/obn-tests/build /Data/obn-tests/src'
# shellcheck disable=SC2086
tar -C "$BUILD" -cf - $programs | "$VM" 'tar -C /Data/obn-tests/build -xf -'
tar -C "$SRC" -cf - tests/fixtures | "$VM" 'tar -C /Data/obn-tests/src -xf -'

script='cd /Data/obn-tests; pass=0; fail=0; failed=
run() { name=$1; dir=$2; shift 2
	start=$(date +%s)
	(cd "$dir" && timeout 120 "$@") > /Data/obn-tests/$name.log 2>&1; rc=$?
	end=$(date +%s)
	if [ $rc = 0 ]; then pass=$((pass+1)); r=PASS; else fail=$((fail+1)); failed="$failed $name"; r="FAIL($rc)"; fi
	echo "$r $name $((end-start))s"
}
'
while read -r name dir cmd; do
	script+="run $name $dir $cmd"$'\n'
done <<< "$list"
script+='echo "passed $pass, failed $fail:$failed"; [ $fail = 0 ]'
"$VM" "$script"
