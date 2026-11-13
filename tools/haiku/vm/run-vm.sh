#!/usr/bin/env bash
# run-vm.sh ARCH PAYLOAD_DIR
#
# Boots a current air/OS image in QEMU with PAYLOAD_DIR as a second (BFS)
# volume, runs PAYLOAD_DIR/run.sh in the guest, shuts the guest down and
# copies what the script left in /payload/out back to PAYLOAD_DIR/out (both
# ways as a tar archive on the volume). The
# script's output is PAYLOAD_DIR/out/output.log, its exit status
# PAYLOAD_DIR/out/exit-code; the guest's serial log is VM_DIR/serial.log.
#
#   x86_64  KVM, BIOS boot of the image as a disk
#   arm64   emulated Cortex-A72 (TCG), UEFI, the image as a USB disk, as
#           airos-ci's smoke test boots it: several times slower than a board
#
# The air/OS images have no SSH server, so there is no interactive access:
# everything a test needs goes in through the payload and comes back the
# same way (for a look at the screen, set VM_VNC to a free display number).
#
# Environment:
#   AIROS_IMAGES  where the images are (default /data2/airos/artifacts/images)
#   VM_DIR        scratch for the decompressed image, the overlay and the
#                 payload volume (default BS_SCRATCH/vm-core/ARCH)
#   VM_MEMORY, VM_CPUS, VM_TIMEOUT (seconds), VM_PAYLOAD_MB (volume size)
#   VM_VNC        VNC display number N (127.0.0.1:5900+N); none by default
#   VM_KEEP=1     do not shut down after run.sh (stop QEMU yourself)
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ARCH=${1:?usage: run-vm.sh x86_64|arm64 payload-dir}
PAYLOAD=$(readlink -f "${2:?usage: run-vm.sh x86_64|arm64 payload-dir}")
. "$HERE/../env.sh" "$ARCH"
[[ -f $PAYLOAD/run.sh ]] || { echo "run-vm.sh: no run.sh in $PAYLOAD" >&2; exit 1; }

IMAGES=${AIROS_IMAGES:-/data2/airos/artifacts/images}
VM_DIR=${VM_DIR:-$BS_SCRATCH/vm-core/$HAIKU_ARCH}
BFS_SHELL=$TOOLS/bfs_shell/bfs_shell
[[ -x $BFS_SHELL ]] || { echo "run-vm.sh: no bfs_shell in $TOOLS" >&2; exit 1; }
mkdir -p "$VM_DIR"
if [[ -f $VM_DIR/qemu.pid ]] && kill -0 "$(cat "$VM_DIR/qemu.pid")" 2>/dev/null; then
	echo "run-vm.sh: a VM is already running from $VM_DIR" >&2
	exit 1
fi

# --- the system disk: the newest image, with the runner job added, once
image=$(readlink -f "$IMAGES/$HAIKU_ARCH/latest")
xz=$(ls "$image"/*.iso.xz | head -n 1)
base=$VM_DIR/base-$(basename "$image").raw
if [[ ! -f $base.ready ]]; then
	rm -f "$VM_DIR"/base-*.raw "$VM_DIR"/base-*.ready "$VM_DIR/overlay.qcow2"
	echo "decompressing $(basename "$xz")" >&2
	xz -dc "$xz" > "$base"
	# The BFS partition inside the image (its MBR or GPT entry).
	read -r start end < <(python3 - "$base" <<'PY'
import json, subprocess, sys
table = json.loads(subprocess.check_output(['/sbin/sfdisk', '-J', sys.argv[1]]))['partitiontable']
unit = table.get('sectorsize', 512)
parts = [p for p in table['partitions'] if p['type'].lower() in ('eb', '42465331-3ba3-10f1-802a-4861696b7521')]
p = max(parts, key=lambda p: p['size'])
print(p['start'] * unit, (p['start'] + p['size']) * unit)
PY
	)
	work=$(mktemp -d "$VM_DIR/runner-XXXXXX")
	cat > "$work/vm-runner.sh" <<'RUNNER'
#!/bin/sh
# Added by tools/haiku/vm/run-vm.sh: runs the payload volume's run.sh once
# at boot and shuts down. Everything also goes to the serial log.
exec >/dev/dprintf 2>&1
echo "VM RUNNER START"
i=0
while [ ! -f /payload/run.sh ] && [ $i -lt 90 ]; do
	mountvolume payload >/dev/null 2>&1
	sleep 1
	i=$((i + 1))
done
if [ ! -f /payload/run.sh ]; then
	echo "VM RUNNER: no payload volume"
	exit 0
fi
cd /payload
if [ -f payload.tar ]; then
	tar -xf payload.tar && rm payload.tar
fi
mkdir -p /payload/out
( sh /payload/run.sh; echo $? > /payload/out/exit-code ) 2>&1 | tee /payload/out/output.log
echo "VM RUNNER EXIT $(cat /payload/out/exit-code)"
tar -cf /payload/out.tar out
sync
if [ -f /payload/keep-running ]; then
	echo "VM RUNNER DONE (kept running)"
	exit 0
fi
cd /
unmount /payload
echo "VM RUNNER DONE"
shutdown -q
RUNNER
	cat > "$work/job" <<'JOB'
# Added by tools/haiku/vm/run-vm.sh (Bambu Studio test VM).
job x-vnd.bambustudio-vm-runner {
	launch /bin/sh /boot/home/vm-runner.sh
}
JOB
	"$BFS_SHELL" --start-offset "$start" --end-offset "$end" "$base" > "$work/log" 2>&1 <<COMMANDS
mkdir -p /myfs/system/settings/launch
cp :$work/vm-runner.sh /myfs/home/vm-runner.sh
chmod 755 /myfs/home/vm-runner.sh
cp :$work/job /myfs/system/settings/launch/bambustudio-vm-runner
ls /myfs/home
sync
quit
COMMANDS
	grep -q vm-runner.sh "$work/log" || { cat "$work/log" >&2; echo "run-vm.sh: could not add the runner to the image" >&2; exit 1; }
	rm -rf "$work"
	touch "$base.ready"
fi
rm -f "$VM_DIR/overlay.qcow2"
qemu-img create -q -f qcow2 -b "$base" -F raw "$VM_DIR/overlay.qcow2"

# --- the payload volume
payload=$VM_DIR/payload.bfs
rm -f "$payload"
truncate -s "${VM_PAYLOAD_MB:-2048}M" "$payload"
"$BFS_SHELL" --initialize "$payload" payload > "$VM_DIR/payload.log" 2>&1 \
	|| { cat "$VM_DIR/payload.log" >&2; exit 1; }
# As one archive, which the guest unpacks: bfs_shell's recursive copy of a
# large tree runs out of transaction space and crashes.
tar -cf "$VM_DIR/payload.tar" -C "$PAYLOAD" --exclude=./out --exclude=./run.sh .
{
	echo "cp :$VM_DIR/payload.tar /myfs/payload.tar"
	echo "cp :$PAYLOAD/run.sh /myfs/run.sh"
	if [[ ${VM_KEEP:-0} == 1 ]]; then echo "mkdir /myfs/keep-running"; fi
	echo sync
	echo quit
} | "$BFS_SHELL" "$payload" >> "$VM_DIR/payload.log" 2>&1 \
	|| { tail -20 "$VM_DIR/payload.log" >&2; exit 1; }
rm -f "$VM_DIR/payload.tar"

# --- boot
serial=$VM_DIR/serial.log
: > "$serial"
common=(-display none -serial "file:$serial" -no-reboot -pidfile "$VM_DIR/qemu.pid"
	-monitor "unix:$VM_DIR/monitor.sock,server,nowait")
case $HAIKU_ARCH in
x86_64)
	qemu=(qemu-system-x86_64 -enable-kvm -cpu host -m "${VM_MEMORY:-8192}" -smp "${VM_CPUS:-8}"
		-drive "file=$VM_DIR/overlay.qcow2,format=qcow2,if=ide,index=0"
		-drive "file=$payload,format=raw,if=ide,index=1"
		-nic user,model=e1000 -device qemu-xhci -device usb-tablet -vga std)
	;;
arm64)
	fw=/usr/share/AAVMF/AAVMF_CODE.fd
	[[ -f $fw ]] || fw=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
	cp /usr/share/AAVMF/AAVMF_VARS.fd "$VM_DIR/vars.fd" 2>/dev/null || truncate -s 64M "$VM_DIR/vars.fd"
	qemu=(qemu-system-aarch64 -M virt -cpu cortex-a72 -m "${VM_MEMORY:-4096}" -smp "${VM_CPUS:-4}"
		-drive "if=pflash,format=raw,readonly=on,file=$fw"
		-drive "if=pflash,format=raw,file=$VM_DIR/vars.fd"
		-device qemu-xhci,id=xhci,p2=15,p3=15
		-device usb-kbd,bus=xhci.0,port=10 -device usb-tablet,bus=xhci.0,port=12
		-drive "if=none,id=stick,format=qcow2,file=$VM_DIR/overlay.qcow2"
		-device usb-storage,bus=xhci.0,port=1,drive=stick
		-drive "if=none,id=payload,format=raw,file=$payload"
		-device usb-storage,bus=xhci.0,port=2,drive=payload
		-device ramfb)
	;;
esac
[[ -z ${VM_VNC:-} ]] || common+=(-vnc "127.0.0.1:$VM_VNC")
"${qemu[@]}" "${common[@]}" &
qemu_pid=$!
trap 'kill $qemu_pid 2>/dev/null || true' EXIT
timeout=${VM_TIMEOUT:-1800}
state=timeout
for ((i = 0; i < timeout; i += 2)); do
	sleep 2
	if ! kill -0 $qemu_pid 2>/dev/null; then
		state=off
		break
	fi
	if grep -aqE "Welcome to Kernel Debugging Land|PANIC:" "$serial"; then
		state=panic
		break
	fi
	if grep -aq "VM RUNNER DONE" "$serial"; then
		state=done
		[[ ${VM_KEEP:-0} == 1 ]] && break
		# give the shutdown a moment to power the machine off
		for ((j = 0; j < 30; j++)); do
			kill -0 $qemu_pid 2>/dev/null || break
			sleep 1
		done
		break
	fi
done
if [[ ${VM_KEEP:-0} == 1 && $state == done ]]; then
	trap - EXIT
	echo "the guest keeps running (pid $qemu_pid)" >&2
	exit 0
fi
kill $qemu_pid 2>/dev/null || true
wait $qemu_pid 2>/dev/null || true
trap - EXIT
rm -f "$VM_DIR/qemu.pid"

# --- results
rm -rf "$PAYLOAD/out" "$VM_DIR/out.tar"
mkdir -p "$PAYLOAD/out"
"$BFS_SHELL" "$payload" >> "$VM_DIR/payload.log" 2>&1 <<COMMANDS || true
cp /myfs/out.tar :$VM_DIR/out.tar
quit
COMMANDS
[[ ! -f $VM_DIR/out.tar ]] || tar -xf "$VM_DIR/out.tar" -C "$PAYLOAD"
rm -f "$VM_DIR/out.tar"
case $state in
done | off) ;;
*)
	echo "run-vm.sh: the guest did not finish ($state); see $serial" >&2
	exit 1
	;;
esac
[[ -f $PAYLOAD/out/exit-code ]] || { echo "run-vm.sh: run.sh left no exit status; see $serial" >&2; exit 1; }
exit "$(cat "$PAYLOAD/out/exit-code")"
