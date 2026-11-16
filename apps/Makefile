CC ?= cc
BUILD_DIR ?= ../app-output
CFLAGS ?= -Os -Wall -Wextra -Werror
.PHONY: all check
all: $(BUILD_DIR)/pocketos-info $(BUILD_DIR)/pocketos-r1-runtime $(BUILD_DIR)/pocketos-shell $(BUILD_DIR)/pocketos-wifi $(BUILD_DIR)/pocketos-bluetooth $(BUILD_DIR)/pocketos-update $(BUILD_DIR)/pocketos-decoder $(BUILD_DIR)/pocketos-music

$(BUILD_DIR)/pocketos-info: pocketos-info.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-r1-runtime: pocketos-r1-runtime.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-reader-font.h: generate-reader-font.py
	mkdir -p "$(BUILD_DIR)"
	/usr/bin/python3 generate-reader-font.py "$@"
	cp /usr/share/doc/fonts-dejavu-core/copyright "$(BUILD_DIR)/reader-font-LICENSE.txt"

$(BUILD_DIR)/pocketos-shell: pocketos-shell.c pocketos-audio-formats.h pocketos-files.h pocketos-radio-ui.h $(BUILD_DIR)/pocketos-reader-font.h
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

$(BUILD_DIR)/pocketos-music: pocketos-music.c pocketos-audio-formats.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

check: all
	test "$$($(BUILD_DIR)/pocketos-info --version)" = "PocketOS application SDK smoke test v1"
	sh test-r1-runtime.sh "$(abspath $(BUILD_DIR)/pocketos-r1-runtime)"
	sh test-shell.sh "$(abspath $(BUILD_DIR)/pocketos-shell)"
	python3 test-wifi.py "$(abspath $(BUILD_DIR)/pocketos-wifi)"
	sh -n pocketos-bluetooth.sh
	sh test-update.sh "$(abspath $(BUILD_DIR)/pocketos-update)" pack-update.py
	python3 test-music.py "$(abspath $(BUILD_DIR)/pocketos-music)"
	python3 test-books.py prepare-books.py
	sh test-decoder.sh "$(abspath $(BUILD_DIR)/pocketos-decoder)"
