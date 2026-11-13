#!/usr/bin/env bash
# Starts the Bambu Studio GUI test VM (air/OS x86_64; VNC 127.0.0.1:5918,
# SSH 2248; software GL). Disk: BS_VM_DIR/disk.img from make-disk.sh.
#   BS_VM_MEMORY (MiB, default 8192), BS_VM_CPUS (default 6),
#   BS_VM_VNC (display number, default 18), BS_VM_SSH (default 2248)
set -euo pipefail
DIR=${BS_VM_DIR:-/data1/scratch/bambustudio/vm-gui}
if [[ -f $DIR/qemu.pid ]] && kill -0 "$(cat "$DIR/qemu.pid")" 2>/dev/null; then
	echo 'The GUI test VM is already running.'; exit 0
fi
rm -f "$DIR/qmp.sock"
exec qemu-system-x86_64 -enable-kvm -cpu host -m "${BS_VM_MEMORY:-8192}" \
	-smp "${BS_VM_CPUS:-6}" \
	-drive file="$DIR/disk.img",format=raw,if=ide,index=0 \
	-nic user,model=e1000,hostfwd=tcp:127.0.0.1:"${BS_VM_SSH:-2248}"-:22 \
	-device qemu-xhci -device usb-tablet -vga std -display none \
	-vnc 127.0.0.1:"${BS_VM_VNC:-18}" -qmp unix:"$DIR/qmp.sock",server=on,wait=off \
	-pidfile "$DIR/qemu.pid" -serial file:"$DIR/serial.log" -daemonize
