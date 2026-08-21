#!/bin/sh
# Contrarian boot entry. Called from .tmp_update/tg5040.sh; never returns.

CTR_DIR=/mnt/SDCARD/Contrarian
SDCARD=/mnt/SDCARD

export PLATFORM=tg5040
export DEVICE=brick
export SDCARD_PATH=$SDCARD
export BIOS_PATH=$SDCARD/Bios
export ROMS_PATH=$SDCARD
export SAVES_PATH=$SDCARD/Saves
export CHEATS_PATH=$SDCARD/Cheats
export SYSTEM_PATH=$CTR_DIR
export CORES_PATH=$CTR_DIR/cores
export USERDATA_PATH=$SDCARD/.userdata/tg5040
export SHARED_USERDATA_PATH=$SDCARD/.userdata/shared
export LOGS_PATH=$USERDATA_PATH/logs

# Cleared by the boot animation when it finishes; the launcher waits on it
# before presenting its first frame. See the animation block below.
CTR_ANIM_FLAG=/tmp/contrarian_bootanim
export CTR_ANIM_FLAG
export HOME=$USERDATA_PATH
export LD_LIBRARY_PATH=$CTR_DIR/lib:/usr/trimui/lib:$LD_LIBRARY_PATH
export PATH=/usr/trimui/bin:$PATH

mkdir -p "$SAVES_PATH" "$USERDATA_PATH" "$LOGS_PATH" "$SHARED_USERDATA_PATH" \
         "$CTR_DIR/faces"

CFG=$CTR_DIR/contrarian.cfg
getcfg() { [ -f "$CFG" ] && sed -n "s/^$1=//p" "$CFG" | tail -1; }

# All LEDs off: saves power, and Contrarian's only meaningful light is the one
# painted on the focused television. Re-run after trimui_inputd, which
# re-lights them when it starts.
leds_off() {
	echo 0 > /sys/class/led_anim/effect_enable 2> /dev/null
	for g in l r lr m f1 f2; do echo "000000 " > /sys/class/led_anim/effect_rgb_hex_$g 2> /dev/null; done
	echo 0 > /sys/class/led_anim/max_scale 2> /dev/null
	echo 0 > /sys/class/led_anim/max_scale_lr 2> /dev/null
	echo 0 > /sys/class/led_anim/max_scale_f1f2 2> /dev/null
	for f in /sys/class/leds/sunxi_led*/brightness; do echo 0 > "$f" 2> /dev/null; done
}

# Apply the configured brightness now (raw table matches libmsettings) so the
# boot animation is not dim -- contrarian.elf has not started yet.
brightness_raw() {
	case "$(getcfg brightness)" in
		0) echo 1;; 1) echo 8;; 2) echo 16;; 3) echo 32;; 4) echo 48;; 5) echo 72;;
		6) echo 96;; 7) echo 128;; 8) echo 160;; 9) echo 192;; 10) echo 255;; *) echo 160;;
	esac
}
[ -x "$CTR_DIR/setbright" ] && "$CTR_DIR/setbright" "$(brightness_raw)"
leds_off

# One-time: replace the stock u-boot splash with a black frame, so the handoff
# into the boot animation is seamless rather than a TrimUI logo followed by a
# cut. Guarded by a marker; the stock logo is backed up first.
if [ -f "$CTR_DIR/bootlogo.bmp" ] && [ ! -f "$CTR_DIR/.bootlogo_applied" ]; then
	mkdir -p /mnt/boot
	if mount -t vfat /dev/mmcblk0p1 /mnt/boot 2> /dev/null; then
		if [ -f /mnt/boot/bootlogo.bmp ] && [ ! -f "$CTR_DIR/bootlogo.stock.bmp" ]; then
			cp /mnt/boot/bootlogo.bmp "$CTR_DIR/bootlogo.stock.bmp"
		fi
		cp "$CTR_DIR/bootlogo.bmp" /mnt/boot/bootlogo.bmp && sync
		umount /mnt/boot && touch "$CTR_DIR/.bootlogo_applied"
	fi
fi

# One-time: the stock "loading" splash that pic2fb blits from /etc/splash.png.
if [ -f "$CTR_DIR/splash.png" ] && [ ! -f "$CTR_DIR/.splash_applied" ]; then
	if [ -f /etc/splash.png ] && [ ! -f "$CTR_DIR/splash.stock.png" ]; then
		cp /etc/splash.png "$CTR_DIR/splash.stock.png"
	fi
	cp "$CTR_DIR/splash.png" /etc/splash.png && sync && touch "$CTR_DIR/.splash_applied"
fi

# One-time: pic2fb blits the splash at the hardware-default brightness, which
# differs from ours, so the screen visibly jumps partway through boot. Call the
# brightness helper before it. Reversible: stock init backed up alongside.
if [ -x "$CTR_DIR/setbright" ] && [ ! -f "$CTR_DIR/.brightboot_applied" ]; then
	cp "$CTR_DIR/setbright" /usr/trimui/bin/setbright 2> /dev/null && chmod +x /usr/trimui/bin/setbright
	cat > /usr/trimui/bin/contrarian-bootbright.sh <<'BB'
#!/bin/sh
BR=$(sed -n 's/^brightness=//p' /mnt/SDCARD/Contrarian/contrarian.cfg 2> /dev/null | tail -1)
case "$BR" in
	0) R=1;; 1) R=8;; 2) R=16;; 3) R=32;; 4) R=48;; 5) R=72;;
	6) R=96;; 7) R=128;; 8) R=160;; 9) R=192;; 10) R=255;; *) R=160;;
esac
[ -x /usr/trimui/bin/setbright ] && /usr/trimui/bin/setbright "$R"
BB
	chmod +x /usr/trimui/bin/contrarian-bootbright.sh
	if [ -f /etc/init.d/runtrimui ] && ! grep -q contrarian-bootbright /etc/init.d/runtrimui; then
		cp /etc/init.d/runtrimui /etc/init.d/runtrimui.ctr-bak
		awk '/pic2fb/ && !d {print "/usr/trimui/bin/contrarian-bootbright.sh"; d=1} {print}' \
			/etc/init.d/runtrimui.ctr-bak > /etc/init.d/runtrimui
		sh -n /etc/init.d/runtrimui 2> /dev/null || cp /etc/init.d/runtrimui.ctr-bak /etc/init.d/runtrimui
		chmod +x /etc/init.d/runtrimui
	fi
	sync
	touch "$CTR_DIR/.brightboot_applied"
fi

# Boot animation: the Konami Code entered on black, then the logo.
#
# Played in the BACKGROUND, so everything below -- and, more to the point, the
# launcher's own ~830ms of startup -- happens while it is on screen instead of
# after it. An animation that adds its length to the boot is just a delay with
# a picture on it.
#
# ffmpeg and the launcher both write to /dev/fb0, so they must not both be
# drawing: last writer wins and they would fight at 30 vs 60fps. The marker
# file is the handshake -- the launcher does all of its work (scan, GL init,
# asset decode) while this plays, then blocks on the marker and presents its
# first frame the moment the animation clears it. Removed in the same
# subshell so it goes even if ffmpeg dies.
if [ -f "$CTR_DIR/contrarian-boot.mp4" ]; then
	: > "$CTR_ANIM_FLAG"
	(
		ffmpeg -hide_banner -loglevel quiet -re -i "$CTR_DIR/contrarian-boot.mp4" \
		       -pix_fmt bgra -f fbdev /dev/fb0 2> /dev/null
		rm -f "$CTR_ANIM_FLAG"
	) &

	# Free seconds: pull what the first game launch will need off the card and
	# into the page cache while nothing else is using the disk. Cold reads of
	# minarch, the core and its libraries measure ~190ms against ~30ms warm.
	(
		cat "$CTR_DIR/minarch.elf" "$CTR_DIR/cores/"*.so "$CTR_DIR/lib/"* > /dev/null 2>&1
		for z in /mnt/SDCARD/*.zip; do [ -f "$z" ] && cat "$z" > /dev/null 2>&1; done
	) &
fi

# Rumble off, mute-switch gpio readable
echo 227 > /sys/class/gpio/export 2> /dev/null
echo -n out > /sys/class/gpio/gpio227/direction 2> /dev/null
echo -n 0 > /sys/class/gpio/gpio227/value 2> /dev/null

# Radio silence, unless this is a development unit.
#
# `wifi=1` in contrarian.cfg -- or a `.devwifi` marker file -- keeps WiFi up so
# the device stays reachable over SSH. Without it there is no way to diagnose a
# problem on hardware except by pulling the card, which is a bad place to be
# when something does not come up.
#
# Stop the supplicant and drop the interface rather than rfkill-blocking, so
# the radio is left in a state the firmware understands.
WIFI=$(getcfg wifi)
if [ "$WIFI" = "1" ] || [ -f "$CTR_DIR/.devwifi" ]; then
	sh /etc/wifi/wifi_init.sh start > /dev/null 2>&1 &
else
	/etc/init.d/wpa_supplicant stop 2> /dev/null
	killall -q udhcpc wpa_supplicant 2> /dev/null
	ifconfig wlan0 down 2> /dev/null
fi

# Bluetooth off, always. Contrarian has nothing to pair with, and the radio is
# pure battery drain on a device whose entire job is one 1988 cartridge.
killall -q bluealsa bluetoothd 2> /dev/null
/etc/init.d/bluetooth stop 2> /dev/null
rfkill block bluetooth 2> /dev/null
echo 0 > /sys/class/rfkill/rfkill0/state 2> /dev/null

# CPU: interactive scaling
echo interactive > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2> /dev/null

# EVERY button and the d-pad arrive through the stock GPIO input daemon's
# virtual joystick at /dev/input/event3. Without this daemon running there is
# no d-pad and no face buttons at all -- only volume and power, which come from
# real kernel devices at event0/event1. It is the single line the whole control
# scheme depends on.
pgrep trimui_inputd > /dev/null || trimui_inputd &

# LEDs off again: trimui_inputd re-enables them when it starts.
sleep 1
leds_off

# One log per boot, carrying the launcher AND anything it execs (minarch
# included). Without this, a failure inside a game goes to a console nobody
# reads and has to be reproduced by hand over adb.
LOG=$CTR_DIR/contrarian.log
[ -f "$LOG" ] && mv -f "$LOG" "$CTR_DIR/contrarian.log.1"
: > "$LOG"

# Resident minarch: it holds the GL context and the loaded core between games,
# which takes a launch from ~1100ms to ~200ms. Started here so its ~1s of setup
# runs during the boot animation, alongside the launcher's own -- and so any
# clearing it does while creating its context is hidden under the animation
# rather than flashing over the launcher.
#
# Nothing depends on it: the launcher checks for its fifos and runs a game the
# old way, one process per game, if they are not there. That is what happens
# for a launch in the first second after boot, and if this ever dies.
export CONTRARIAN_TAG=Contra
export CONTRARIAN_PANEL=$CTR_DIR/res/nes

start_resident() {
	pgrep -f "minarch.elf --resident" > /dev/null && return
	"$CTR_DIR/minarch.elf" --resident "$CTR_DIR/cores/fceumm_libretro.so" \
		>> "$LOG" 2>&1 &
}
start_resident

# Restart loop: only ever exits for poweroff.
while true; do
	leds_off
	start_resident          # bring it back if it died
	"$CTR_DIR/contrarian.elf" >> "$LOG" 2>&1
	if [ -f /tmp/contrarian_poweroff ]; then
		rm -f /tmp/contrarian_poweroff
		sync
		poweroff
		sleep 10
	fi
	sleep 1
done
