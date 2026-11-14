#!/usr/bin/env bash
# Boots the GUI test VM (tools/haiku/vm/gui) for the network plug-in tests,
# wired to the mock printer of mock-printer.py on the host:
#
#   tools/haiku/test/mock-printer.py &        # on the host
#   tools/haiku/test/run-mock-vm.sh           # VNC 127.0.0.1:5919, SSH 2249
#
# QEMU's user network cannot carry the printer's ports (990 is privileged on
# the host) or its SSDP broadcasts, so the guest sees the mock as a printer
# at MOCK_IP (default 10.0.2.100): every connection to one of its ports is
# handed (socat, per connection) to the mock's port on the host, 10000 +
# the printer's port (PORTS below; the FTPS data port 50000 is 15000), and
# host UDP port 12021 reaches the guest's SSDP port 2021, where the mock
# sends its NOTIFY messages.
# The other VM settings (BS_VM_DIR, memory, CPUs) are run-vm.sh's.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export BS_VM_DIR=${BS_VM_DIR:-/data1/scratch/bambustudio/vm-obn}
export BS_VM_VNC=${BS_VM_VNC:-19} BS_VM_SSH=${BS_VM_SSH:-2249}
MOCK_IP=${MOCK_IP:-10.0.2.100}
# guest port -> host port of mock-printer.py (keep in sync with its defaults)
PORTS="8883:18883 990:10990 3000:13000 6000:16000 50000:15000"
net="hostfwd=udp:127.0.0.1:${MOCK_SSDP_PORT:-12021}-:2021"
for pair in $PORTS; do
	net+=",guestfwd=tcp:$MOCK_IP:${pair%%:*}-cmd:socat - TCP:127.0.0.1:${pair##*:}"
done
BS_VM_NET_EXTRA=$net exec "$HERE/../vm/gui/run-vm.sh"
