#!/usr/bin/env bash
# Builds the Bambu Studio GUI test VM disk from an air/OS x86_64 anyboot ISO, on Linux
# with the Haiku host tools (bfs_shell): copies the ISO's MBR and BFS
# partition into a sparse raw image, adds a "Data" BFS volume on the rest,
# installs an SSH key and starts sshd at boot.
#   tools/haiku/vm/gui/make-disk.sh [ISO]
# BS_VM_DIR (default /data1/scratch/bambustudio/vm-gui) holds disk, key, logs.
set -euo pipefail
DIR=${BS_VM_DIR:-/data1/scratch/bambustudio/vm-gui}
ISO=${1:-$DIR/airos-x86_64.iso}
SIZE_GB=${BS_VM_DISK_GB:-16}
. "${AIROS_SDK:-/data1/airos/sdk}/x86_64/env.sh"
BFS=$TOOLS/bfs_shell/bfs_shell
DISK=$DIR/disk.img
mkdir -p "$DIR"
if [[ -f $DIR/qemu.pid ]] && kill -0 "$(cat "$DIR/qemu.pid")" 2>/dev/null; then
	echo 'Stop the VM first.' >&2; exit 1
fi
[[ -f $DIR/id_ed25519 ]] || ssh-keygen -q -t ed25519 -N '' -C bs-gui-vm -f "$DIR/id_ed25519"
read -r START SECTORS < <(python3 - "$ISO" <<'PY'
import struct, sys
mbr = open(sys.argv[1], 'rb').read(512)
for i in range(4):
	entry = mbr[446 + 16 * i:462 + 16 * i]
	if entry[4] == 0xeb:
		print(*struct.unpack('<II', entry[8:16]))
		break
else:
	sys.exit('no BFS partition in ' + sys.argv[1])
PY
)
echo "BFS partition: sector $START, $SECTORS sectors"
rm -f "$DISK"
truncate -s "${SIZE_GB}G" "$DISK"
dd if="$ISO" of="$DISK" bs=512 count="$START" conv=notrunc,sparse status=none
dd if="$ISO" of="$DISK" bs=1M iflag=skip_bytes,count_bytes oflag=seek_bytes \
	skip=$((START * 512)) seek=$((START * 512)) count=$((SECTORS * 512)) conv=notrunc,sparse status=none
TOTAL=$(( SIZE_GB * 1024 * 1024 * 2 ))
DATA_START=$(( (START + SECTORS + 2047) / 2048 * 2048 ))
DATA_SECTORS=$(( (TOTAL - DATA_START) / 2048 * 2048 ))
python3 - "$DISK" "$START" "$SECTORS" "$DATA_START" "$DATA_SECTORS" <<'PY'
import struct, sys
path = sys.argv[1]
start, count, data_start, data_count = map(int, sys.argv[2:])
def entry(active, start, count):
	return bytes([0x80 if active else 0, 0xfe, 0xff, 0xff, 0xeb, 0xfe, 0xff, 0xff]) + struct.pack('<II', start, count)
with open(path, 'r+b') as f:
	mbr = bytearray(f.read(512))
	keep = mbr[446:462] if mbr[446 + 4] == 0xeb else None
	mbr[446:510] = bytes(64)
	mbr[446:462] = keep if keep else entry(True, start, count)
	mbr[446] = 0x80
	mbr[462:478] = entry(False, data_start, data_count)
	f.seek(0)
	f.write(mbr)
PY
OFFSETS=(--start-offset $(( START * 512 )) --end-offset $(( (START + SECTORS) * 512 )))
DATA_OFFSETS=(--start-offset $(( DATA_START * 512 )) --end-offset $(( (DATA_START + DATA_SECTORS) * 512 )))
"$BFS" "${DATA_OFFSETS[@]}" --initialize "$DISK" Data >/dev/null
# The image has its own UserBootscript: keep it and append.
rm -f "$DIR/UserBootscript"
printf 'cp /myfs/home/config/settings/boot/UserBootscript :%s/UserBootscript\nquit\n' "$DIR" | "$BFS" "${OFFSETS[@]}" "$DISK" >/dev/null 2>&1 || true
[[ -s $DIR/UserBootscript ]] || echo '#!/bin/sh' > "$DIR/UserBootscript"
cat >> "$DIR/UserBootscript" <<'BOOT'

# Bambu Studio GUI test VM: sshd and the Data volume.
/boot/system/bin/ssh-keygen -A >/dev/null 2>&1
/boot/system/bin/sshd
mountvolume Data >/dev/null 2>&1
BOOT
cp "$DIR/id_ed25519.pub" "$DIR/authorized_keys"
# The boot volume of the image is used through a write overlay: what the
# guest writes there is gone after a reboot. Everything that has to be there
# at every boot is written here, into the image.
#  - debug_server: crash reports on the Desktop instead of the debugger alert.
#  - packages (only with BS_VM_BLOCK_ZINK=1): blocks the image's Zink renderer
#    add-on. Do not: without a Vulkan driver that add-on is what renders, with
#    its own softpipe (slow but stable). The "Software Pipe" add-on of
#    mesa_swpipe, which takes over when Zink is blocked, crashes in the first
#    frame of BambuStudio with llvmpipe and with softpipe (2026-11-13).
mkdir -p "$DIR/guest"
echo "default_action report" > "$DIR/guest/debug_server_settings"
cat > "$DIR/guest/packages" <<'PKG'
Package airos_payload {
	BlockedEntries {
		data/airos/non-packaged/add-ons/opengl-zink/Zink
	}
}
PKG
{
	echo "mkdir -p /myfs/home/config/settings/ssh"
	echo "cp :$DIR/authorized_keys /myfs/home/config/settings/ssh/authorized_keys"
	echo "chmod 700 /myfs/home/config/settings/ssh"
	echo "chmod 600 /myfs/home/config/settings/ssh/authorized_keys"
	echo "mkdir -p /myfs/home/config/settings/boot"
	echo "cp :$DIR/UserBootscript /myfs/home/config/settings/boot/UserBootscript"
	echo "chmod 755 /myfs/home/config/settings/boot/UserBootscript"
	echo "mkdir -p /myfs/home/config/settings/system/debug_server"
	echo "cp :$DIR/guest/debug_server_settings /myfs/home/config/settings/system/debug_server/settings"
	[[ ${BS_VM_BLOCK_ZINK:-0} != 1 ]] || echo "cp :$DIR/guest/packages /myfs/system/settings/packages"
	echo "ls /myfs/home/config/settings"
	echo "sync"
	echo "quit"
} | "$BFS" "${OFFSETS[@]}" "$DISK" 2>&1 | tail -20
echo "disk ready: $DISK ($(du -h "$DISK" | cut -f1) used)"
