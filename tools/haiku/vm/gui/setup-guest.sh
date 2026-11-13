#!/usr/bin/env bash
# Per-boot settings of the GUI test VM (run after run-vm.sh and "Try air/OS"):
# crash reports instead of the debugger window, a 1600x900 screen.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
"$HERE/haiku.sh" 'mkdir -p ~/config/settings/system/debug_server
echo "default_action report" > ~/config/settings/system/debug_server/settings
echo y | screenmode '"${BS_VM_MODE:-1600 900 24}"' > /dev/null; screenmode; sync'
