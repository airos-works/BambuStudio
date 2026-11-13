#!/usr/bin/env bash
# Runs a command in the Bambu Studio GUI test VM over SSH. See run-vm.sh.
# The guest makes new host keys at every boot (local VM: they are not checked).
set -euo pipefail
DIR=${BS_VM_DIR:-/data1/scratch/bambustudio/vm-gui}
exec ssh -i "$DIR/id_ed25519" -p "${BS_VM_SSH:-2248}" -o IdentitiesOnly=yes -o BatchMode=yes \
	-o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no -o LogLevel=ERROR \
	-o ConnectTimeout=10 -o ServerAliveInterval=30 user@127.0.0.1 "$@"
