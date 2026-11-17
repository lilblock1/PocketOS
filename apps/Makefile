CC ?= cc
BUILD_DIR ?= ../app-output
CFLAGS ?= -Os -Wall -Wextra -Werror
.PHONY: all check
all: $(BUILD_DIR)/pocketos-recovery-arm $(BUILD_DIR)/pocketos-sd
$(BUILD_DIR)/pocketos-sd: pocketos-sd.sh
	mkdir -p "$(BUILD_DIR)"
	cp "$<" "$@"
	chmod 755 "$@"
$(BUILD_DIR)/pocketos-recovery-arm: pocketos-recovery-arm.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)
all: $(BUILD_DIR)/pocketos-timeout
all: $(BUILD_DIR)/pocketos-userdata
$(BUILD_DIR)/pocketos-userdata: ../tools/r1-userdata.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)
$(BUILD_DIR)/pocketos-timeout: pocketos-timeout.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)
all: $(BUILD_DIR)/pocketos-info $(BUILD_DIR)/pocketos-stat $(BUILD_DIR)/pocketos-book-prepare $(BUILD_DIR)/book-prepare-LICENSE.txt $(BUILD_DIR)/pocketos-r1-runtime $(BUILD_DIR)/pocketos-shell $(BUILD_DIR)/pocketos-wifi $(BUILD_DIR)/pocketos-bluetooth $(BUILD_DIR)/pocketos-update $(BUILD_DIR)/pocketos-decoder $(BUILD_DIR)/pocketos-music $(BUILD_DIR)/pocketos-start $(BUILD_DIR)/S95pocketos

BOOK_SRC = ../third_party/sonix-epub/epub_arena.c ../third_party/sonix-epub/epub_zip.c ../third_party/sonix-epub/epub_xml.c
$(BUILD_DIR)/pocketos-book-prepare: pocketos-book-prepare.c $(BOOK_SRC) ../third_party/sonix-epub/epub_arena.h ../third_party/sonix-epub/epub_zip.h ../third_party/sonix-epub/epub_xml.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) -I../third_party/sonix-epub $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(BOOK_SRC) -lz $(LDLIBS)

$(BUILD_DIR)/book-prepare-LICENSE.txt: ../third_party/sonix-epub/COPYING
	mkdir -p "$(BUILD_DIR)"
	cp "$<" "$@"

$(BUILD_DIR)/pocketos-stat: pocketos-stat.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-info: pocketos-info.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-r1-runtime: pocketos-r1-runtime.c pocketos-hardware.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-reader-font.h: generate-reader-font.py
	mkdir -p "$(BUILD_DIR)"
	/usr/bin/python3 generate-reader-font.py "$@"
	cp /usr/share/doc/fonts-dejavu-core/copyright "$(BUILD_DIR)/reader-font-LICENSE.txt"

$(BUILD_DIR)/pocketos-shell: pocketos-shell.c pocketos-music-tags.h pocketos-hardware.h pocketos-touch.h pocketos-keys.h pocketos-audio-formats.h pocketos-volume.h pocketos-bt-audio.h pocketos-files.h pocketos-radio-ui.h pocketos-update-ui.h pocketos-power.h pocketos-suspend.h $(BUILD_DIR)/pocketos-reader-font.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) -I"$(BUILD_DIR)" $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-wifi: pocketos-wifi.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-bluetooth: pocketos-bluetooth.sh
	mkdir -p "$(BUILD_DIR)"
	cp "$<" "$@"
	chmod 755 "$@"

$(BUILD_DIR)/pocketos-update: pocketos-update.sh
	mkdir -p "$(BUILD_DIR)"
	cp "$<" "$@"
	chmod 755 "$@"

$(BUILD_DIR)/pocketos-decoder: pocketos-decoder.sh
	mkdir -p "$(BUILD_DIR)"
	cp "$<" "$@"
	chmod 755 "$@"

$(BUILD_DIR)/pocketos-music: pocketos-music.c pocketos-audio-formats.h pocketos-volume.h pocketos-bt-audio.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-start: pocketos-start.sh
	mkdir -p "$(BUILD_DIR)"
	cp "$<" "$@"
	chmod 755 "$@"

$(BUILD_DIR)/S95pocketos: ../boards/hiby-r1/rootfs-overlay/etc/init.d/S95pocketos
	mkdir -p "$(BUILD_DIR)"
	cp "$<" "$@"
	chmod 755 "$@"

check: all
	"$(BUILD_DIR)/pocketos-recovery-arm" --self-test
	python3 test-timeout.py "$(abspath $(BUILD_DIR)/pocketos-timeout)"
	"$(BUILD_DIR)/pocketos-shell" --test-smart-sleep
	python3 test-stat.py "$(abspath $(BUILD_DIR)/pocketos-stat)"
	python3 test-native-books.py "$(abspath $(BUILD_DIR)/pocketos-book-prepare)"
	python3 test-reader-fonts.py "$(abspath $(BUILD_DIR)/pocketos-shell)" prepare-reader-font.py
	test "$$($(BUILD_DIR)/pocketos-info --version)" = "PocketOS application SDK smoke test v1"
	sh test-r1-runtime.sh "$(abspath $(BUILD_DIR)/pocketos-r1-runtime)"
	python3 test-hardware.py "$(abspath $(BUILD_DIR)/pocketos-r1-runtime)" "$(abspath $(BUILD_DIR)/pocketos-shell)"
	sh test-shell.sh "$(abspath $(BUILD_DIR)/pocketos-shell)"
	python3 test-metadata.py "$(abspath $(BUILD_DIR)/pocketos-shell)"
	python3 test-wifi.py "$(abspath $(BUILD_DIR)/pocketos-wifi)"
	sh -n pocketos-bluetooth.sh
	python3 test-bluetooth.py "$(abspath $(BUILD_DIR)/pocketos-bluetooth)"
	sh test-update.sh "$(abspath $(BUILD_DIR)/pocketos-update)" pack-update.py "$(abspath $(BUILD_DIR)/pocketos-stat)"
	python3 test-music.py "$(abspath $(BUILD_DIR)/pocketos-music)"
	python3 test-books.py prepare-books.py
	sh test-decoder.sh "$(abspath $(BUILD_DIR)/pocketos-decoder)"
	sh test-start.sh "$(abspath $(BUILD_DIR)/pocketos-start)"
	sh test-sd.sh "$(abspath $(BUILD_DIR)/pocketos-sd)"
	sh -n "$(BUILD_DIR)/S95pocketos"
