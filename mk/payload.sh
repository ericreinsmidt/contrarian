#!/bin/sh
# Assemble the installable SD payload under out/sd/.
# Copy the CONTENTS of out/sd/ to the root of a FAT32 card, insert into a stock
# Brick, power on: first boot installs the runtrimui.sh hook, every boot after
# goes straight to Contrarian.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/out/sd
C=$OUT/Contrarian

[ -f "$ROOT/build/contrarian.elf" ] || { echo "run make first"; exit 1; }
[ -f "$ROOT/vendor/minarch.elf" ]   || { echo "run minarch/build.sh first"; exit 1; }
[ -f "$ROOT/res/contra.fp" ]        || { echo "run mkfp first"; exit 1; }

rm -rf "$OUT"
mkdir -p "$C/res/room" "$C/res/audio" "$C/res/nes" "$C/cores" "$C/lib" "$C/bin" "$C/faces" "$C/patches" \
         "$OUT/.tmp_update" "$OUT/trimui/app" "$OUT/Saves" "$OUT/Bios" \
         "$OUT/.system/res" "$OUT/.system/tg5040/shaders" \
         "$OUT/Shaders/glsl" "$OUT/.userdata/shared" \
         "$OUT/Overlays/Contra" "$OUT/.system/tg5040/paks/Emus/Contra.pak" \
         "$OUT/The_Only_Game_That_Matters"

cp "$ROOT/build/contrarian.elf" "$C/"
cp "$ROOT/build/setbright"      "$C/"
cp "$ROOT/sd/contrarian/launch.sh" "$C/"
cp "$ROOT/vendor/minarch.elf"   "$C/"
cp "$ROOT/vendor/cores/fceumm_libretro.so" "$C/cores/"
cp "$ROOT/vendor/lib/"*         "$C/lib/"
cp "$ROOT/vendor/bin/governor.sh" "$C/bin/"

# the verification engine's data: the known set, and the base fingerprints that
# let an unknown ROM be scored on a card holding no original ROM at all
# Unblocks minarch's Config_init(); see the file's own comment for why.
cp "$ROOT/config/paks/default.cfg" "$OUT/.system/tg5040/paks/Emus/Contra.pak/default.cfg"

cp "$ROOT/config/contra.db"      "$C/"
cp "$ROOT/config/contrarian.cfg" "$C/"
cp "$ROOT/res/contra.fp"         "$C/"

# one folder per view; view_*.c load from res/<view-assets>/
cp "$ROOT/res/room/"*.png        "$C/res/room/"
cp "$ROOT/res/audio/"*.wav       "$C/res/audio/"
cp "$ROOT/res/nes/"*.png         "$C/res/nes/"

# The same cabinet art serves as minarch's in-game overlay. minarch resolves
# Overlays/<tag>/ at a compile-time root, so it cannot live under Contrarian/.
cp "$ROOT/res/room/set.png"      "$OUT/Overlays/Contra/cabinet.png"

# minarch resolves shaders from a COMPILE-TIME SHADERS_FOLDER (/mnt/SDCARD/Shaders,
# with the sources in its glsl/ subfolder). They cannot live under Contrarian/.
cp "$ROOT/res/shaders/"*.glsl    "$OUT/Shaders/glsl/"

# minarch's own "simple mode": collapses its menu to
# Continue / Save / Load / Reset / Quit and drops the Options tree entirely.
# It is a marker file, so the stripped menu costs no code change at all.
touch "$OUT/.userdata/shared/enable-simple-mode"
cp "$ROOT/res/boot/contrarian-boot.mp4" "$C/"
cp "$ROOT/res/boot/bootlogo.bmp" "$C/"
cp "$ROOT/res/boot/splash.png"   "$C/"
cp "$ROOT/THIRD-PARTY-LICENSES.md" "$C/" 2>/dev/null || true

# minarch resolves /.system/res (fonts) and /.system/tg5040/shaders at compile
# time and segfaults at startup without them
cp "$ROOT/vendor/system/res/"* "$OUT/.system/res/"
cp "$ROOT/vendor/system/tg5040/shaders/"* "$OUT/.system/tg5040/shaders/"


cp "$ROOT/sd/.tmp_update/updater" "$ROOT/sd/.tmp_update/tg5040.sh" "$OUT/.tmp_update/"
cp "$ROOT/sd/trimui/app/MainUI" "$ROOT/sd/trimui/app/runtrimui.sh" "$OUT/trimui/app/"

chmod +x "$OUT/.tmp_update/updater" "$OUT/.tmp_update/tg5040.sh" \
         "$C/launch.sh" "$C/contrarian.elf" "$C/minarch.elf" "$C/setbright" \
         "$C/bin/governor.sh" "$OUT/trimui/app/MainUI" "$OUT/trimui/app/runtrimui.sh"

cat > "$C/patches/README.txt" <<'TXT'
Drop ROMhack patches here. Each one becomes its own channel.

  *.bps            names its own base. The format carries a CRC-32 of the ROM
                   it applies to and of the result it should produce, so
                   Contrarian finds the right base by itself and refuses the
                   patch outright if the result is not what it promised.

  USA/*.ips        IPS carries no verification whatsoever, so the base is named
  JAPAN/*.ips      by the folder. Put the patch in the folder matching the
  EUROPE/*.ips     version it was made for.

The patched ROM is rebuilt in memory at launch and never written to the card,
so nothing derived from your ROMs is stored here.
TXT
mkdir -p "$C/patches/USA" "$C/patches/JAPAN" "$C/patches/EUROPE"

cat > "$OUT/The_Only_Game_That_Matters/PUT ROMS HERE.txt" <<'TXT'
Drop Contra ROMs here -- .nes or .zip.

Strictly speaking anywhere on the card works: Contrarian scans the whole thing.
This folder just saves you from wondering where they go.

Anything that is not Contra will still show up, tuned to static, with a red
circle through it and a number telling you how close it got.
TXT

# A release card carries no development escape hatches: no .devwifi marker, and
# wifi=0 in the config. Assert it rather than trusting it.
if [ -e "$C/.devwifi" ]; then echo "!! .devwifi in payload"; exit 1; fi
if ! grep -q '^wifi=0' "$C/contrarian.cfg"; then
	echo "!! contrarian.cfg does not have wifi=0"; exit 1
fi

du -sh "$OUT"
echo "payload ready: $OUT"
