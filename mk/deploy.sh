#!/bin/sh
# Push a build to a running device over SSH, so iterating never means pulling
# the card. The device has no scp/sftp binary, so everything goes as tar over
# ssh. Usage: mk/deploy.sh [ip] [what]
#   what: all (default) | elf | res | minarch | shaders
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IP=${1:-192.168.1.101}
WHAT=${2:-all}
SSH="sshpass -p tina ssh -o StrictHostKeyChecking=no -o ConnectTimeout=6 root@$IP"
C=/mnt/SDCARD/Contrarian

$SSH "mkdir -p $C/res/room $C/res/tv $C/cores $C/lib $C/faces $C/patches /mnt/SDCARD/Shaders/glsl"

push() {  # push <local-dir> <remote-dir> <files...>
	dir=$1; remote=$2; shift 2
	tar -cf - -C "$dir" "$@" | $SSH "tar -xf - -C $remote"
}

case $WHAT in
elf|all)
	[ -f "$ROOT/build/contrarian.elf" ] || { echo "run make first"; exit 1; }
	push "$ROOT/build" "$C" contrarian.elf setbright
	push "$ROOT/config" "$C" contra.db contrarian.cfg
	push "$ROOT/res" "$C" contra.fp
	push "$ROOT/sd/contrarian" "$C" launch.sh
	$SSH "chmod +x $C/contrarian.elf $C/setbright $C/launch.sh"
	;;
esac
case $WHAT in
res|all)
	push "$ROOT/res/room" "$C/res/room" .
	push "$ROOT/res/tv"   "$C/res/tv"   .
	push "$ROOT/res/boot" "$C" contrarian-boot.mp4
	;;
esac
case $WHAT in
shaders|all)
	push "$ROOT/res/shaders" /mnt/SDCARD/Shaders/glsl contrarian-glow.glsl contrarian-scanline.glsl
	;;
esac
case $WHAT in
minarch|all)
	[ -f "$ROOT/vendor/minarch.elf" ] || { echo "run minarch/build.sh first"; exit 1; }
	push "$ROOT/vendor" "$C" minarch.elf
	$SSH "chmod +x $C/minarch.elf"
	;;
esac

echo "deployed '$WHAT' to $IP"
