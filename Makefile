IMAGE := ghcr.io/loveretro/tg5040-toolchain:latest
BRICK ?= 192.168.1.101
SSH := sshpass -p 'tina' ssh -o StrictHostKeyChecking=no root@$(BRICK)

.PHONY: all clean native harness payload release minarch fingerprints boot install-card \
        deploy deploy-elf restart logs run-remote adb adb-elf adb-shell adb-run adb-log

all: build/contrarian.elf

build/contrarian.elf: src/*.c src/*.h tools/setbright.c mk/cross.mk
	docker run --rm -v $(CURDIR):/work -w /work $(IMAGE) \
		/bin/bash -c 'source ~/.bashrc && make -f mk/cross.mk build/contrarian.elf build/setbright'

native:
	$(MAKE) -f mk/native.mk

# Host harnesses for the verification engine: scan a folder and print a verdict
# per ROM, or fingerprint ROMs and print the similarity matrix. These are how
# the match threshold was derived.
harness:
	mkdir -p build-native
	$(CC) -O2 -std=gnu11 -o build-native/scan_cli   tools/scan_cli.c \
	  src/scan.c src/verify.c src/db.c src/nes.c src/sha1.c src/romfile.c src/patch.c -lz
	$(CC) -O2 -std=gnu11 -o build-native/verify_cli tools/verify_cli.c \
	  src/nes.c src/sha1.c src/romfile.c -lz

# res/contra.fp: base fingerprints, generated from the three original ROMs.
# Only block digests ship -- no ROM content.
fingerprints:
	$(CC) -O2 -std=gnu11 -o build-native/mkfp tools/mkfp.c src/db.c src/nes.c src/sha1.c src/romfile.c -lz
	./build-native/mkfp res/contra.fp \
		"USA=$(CONTRA_USA)" "JAPAN=$(CONTRA_JP)" "EUROPE=$(CONTRA_EU)"

# Assets in res/ are committed ready to ship; nothing needs generating to
# build. This regenerates the boot animation from tools/genboot.py if you want
# to change it.
boot:
	python3 tools/genboot.py

minarch:
	./minarch/build.sh

payload: all
	./mk/payload.sh

# The release artifact: a zip whose CONTENTS go to the root of a FAT32 card.
VERSION ?= 1.0
release: payload
	rm -f out/Contrarian-v$(VERSION).zip
	cd out/sd && zip -qr ../Contrarian-v$(VERSION).zip . -x '.DS_Store' '._*'
	@echo "out/Contrarian-v$(VERSION).zip  $$(du -h out/Contrarian-v$(VERSION).zip | cut -f1)"

# Copy the assembled payload onto a mounted FAT32 card. CARD must be set.
install-card: payload
	@[ -n "$(CARD)" ] || { echo "usage: make install-card CARD=/Volumes/YOURCARD"; exit 1; }
	./mk/install-card.sh "$(CARD)"

# --- ADB over USB: works with no WiFi and no SSH, whenever the device is on.
adb: all
	./mk/adb-deploy.sh all

adb-elf: all
	./mk/adb-deploy.sh elf

adb-shell:
	./mk/adb-deploy.sh --shell 2>/dev/null || adb shell

# Kill the launcher so launch.sh's restart loop picks up a freshly pushed build.
adb-restart:
	adb shell 'killall -q contrarian.elf; exit 0'

# Run the launcher by hand with its output on your terminal -- the fastest way
# to tell "the scan is wrong" from "the display is wrong".
adb-run:
	adb shell 'killall -q contrarian.elf; cd /mnt/SDCARD/Contrarian && \
	  CTR_ROOT=/mnt/SDCARD/Contrarian CTR_CARD=/mnt/SDCARD \
	  LD_LIBRARY_PATH=/mnt/SDCARD/Contrarian/lib:/usr/trimui/lib \
	  ./contrarian.elf 2>&1' | head -40

adb-log:
	adb shell 'dmesg | tail -30; ls -la /mnt/SDCARD/.userdata/tg5040/logs 2>/dev/null'

# --- Push to a running device over SSH -- no card pulling. BRICK=<ip> to override.
deploy: all
	./mk/deploy.sh $(BRICK) all

deploy-elf: all
	./mk/deploy.sh $(BRICK) elf

# Restart the launcher on the device so a pushed binary takes effect.
restart:
	$(SSH) 'killall -q contrarian.elf; exit 0'

# Tail the launcher's stderr from the device.
logs:
	$(SSH) 'cat /mnt/SDCARD/.userdata/tg5040/logs/*.txt 2>/dev/null; \
	        ls -la /mnt/SDCARD/.userdata/tg5040/logs 2>/dev/null'

# Run the launcher by hand over ssh with its output on your terminal.
run-remote:
	$(SSH) 'cd /mnt/SDCARD/Contrarian && killall -q contrarian.elf; \
	        sh -c ". /mnt/SDCARD/Contrarian/launch.sh" 2>&1 | head -40'

clean:
	rm -rf build build-native out
