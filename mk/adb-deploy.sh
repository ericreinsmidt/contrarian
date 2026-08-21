#!/bin/sh
# Push a build to the device over ADB-USB. No card pulling, no WiFi, no SSH.
#
# The TrimUI exposes "TRIMUI ADB" over USB whenever it is powered on, including
# when it has fallen back to stock. Requires the SD card to be IN the device
# and mounted at /mnt/SDCARD -- that is where everything lives.
#
# Usage: mk/adb-deploy.sh [all|elf|res|shaders|minarch]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WHAT=${1:-all}
C=/mnt/SDCARD/Contrarian

# Pick the TrimUI out of whatever else is plugged in.
SER=${ADB_SERIAL:-$(adb devices | awk '/\tdevice$/{print $1}' | while read s; do
	if adb -s "$s" shell 'grep -qi TG.040 /proc/cpuinfo && echo yes' 2>/dev/null | grep -q yes; then
		echo "$s"; break
	fi
done)}
[ -n "$SER" ] || { echo "no TrimUI found over adb (is it powered on?)"; exit 1; }
A="adb -s $SER"

# Is the card actually mounted? This has to be checked by looking at OUTPUT,
# not at an exit status: `adb shell` returns 0 regardless of what the remote
# command exited with, so the obvious `adb shell '... | grep -q ...' || exit`
# is silently always-true. When the card had dropped off the bus mid-session
# that guard waved the deploy through and every push landed in the RAM-backed
# overlay under the empty mountpoint -- reporting success, changing nothing.
case "$($A shell 'mount | grep -q " /mnt/SDCARD " && echo MOUNTED')" in
*MOUNTED*) ;;
*)
	echo "!! /mnt/SDCARD is not mounted on the device -- nothing was deployed."
	echo "   Anything written there now would go to RAM, not the card."
	echo "   Reseat the SD card and reboot the device, then run this again."
	exit 1
	;;
esac

$A shell "mkdir -p $C/res/room $C/res/audio $C/res/nes $C/cores $C/lib $C/faces $C/patches \
          /mnt/SDCARD/.tmp_update \
          /mnt/SDCARD/Shaders/glsl /mnt/SDCARD/.userdata/shared \
          /mnt/SDCARD/Overlays/Contra \
          /mnt/SDCARD/.system/tg5040/paks/Emus/Contra.pak" >/dev/null

case $WHAT in elf|all)
	[ -f "$ROOT/build/contrarian.elf" ] || { echo "run make first"; exit 1; }
	$A push "$ROOT/build/contrarian.elf"     "$C/" >/dev/null
	$A push "$ROOT/build/setbright"          "$C/" >/dev/null
	$A push "$ROOT/config/contra.db"         "$C/" >/dev/null
	$A push "$ROOT/config/contrarian.cfg"    "$C/" >/dev/null
	$A push "$ROOT/res/contra.fp"            "$C/" >/dev/null
	$A push "$ROOT/sd/contrarian/launch.sh"  "$C/" >/dev/null
	$A push "$ROOT/config/paks/default.cfg" \
	        /mnt/SDCARD/.system/tg5040/paks/Emus/Contra.pak/ >/dev/null
	$A push "$ROOT/sd/.tmp_update/updater"   /mnt/SDCARD/.tmp_update/ >/dev/null
	$A push "$ROOT/sd/.tmp_update/tg5040.sh" /mnt/SDCARD/.tmp_update/ >/dev/null
	$A shell "chmod +x $C/contrarian.elf $C/setbright $C/launch.sh \
	          /mnt/SDCARD/.tmp_update/updater /mnt/SDCARD/.tmp_update/tg5040.sh"
	echo "  + launcher"
esac
case $WHAT in res|all)
	$A push "$ROOT/res/room/."               "$C/res/room/" >/dev/null
	$A push "$ROOT/res/audio/."              "$C/res/audio/" >/dev/null
	$A push "$ROOT/res/nes/."                "$C/res/nes/"  >/dev/null
	$A push "$ROOT/res/room/set.png" /mnt/SDCARD/Overlays/Contra/cabinet.png >/dev/null
	$A push "$ROOT/res/boot/contrarian-boot.mp4" "$C/"      >/dev/null
	echo "  + assets"
esac
case $WHAT in shaders|all)
	$A push "$ROOT/res/shaders/contrarian-glow.glsl"     /mnt/SDCARD/Shaders/glsl/ >/dev/null
	$A push "$ROOT/res/shaders/contrarian-scanline.glsl" /mnt/SDCARD/Shaders/glsl/ >/dev/null
	echo "  + shaders"
esac
case $WHAT in minarch|all)
	[ -f "$ROOT/vendor/minarch.elf" ] || { echo "run minarch/build.sh first"; exit 1; }
	$A push "$ROOT/vendor/minarch.elf" "$C/" >/dev/null
	$A shell "chmod +x $C/minarch.elf"
	echo "  + minarch"
esac
$A shell sync
echo "deployed '$WHAT' to $SER"
