CC ?= cc
BUILD_DIR ?= ../app-output
CFLAGS ?= -Os -Wall -Wextra -Werror
.PHONY: all check
all: $(BUILD_DIR)/pocketos-info $(BUILD_DIR)/pocketos-r1-runtime $(BUILD_DIR)/pocketos-shell $(BUILD_DIR)/pocketos-wifi $(BUILD_DIR)/pocketos-bluetooth $(BUILD_DIR)/pocketos-radio-observe $(BUILD_DIR)/pocketos-update $(BUILD_DIR)/pocketos-decoder $(BUILD_DIR)/pocketos-music $(BUILD_DIR)/pocketos-audio-monitor $(BUILD_DIR)/pocketos-transfer $(BUILD_DIR)/pocketos-start $(BUILD_DIR)/S95pocketos

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

$(BUILD_DIR)/pocketos-shell: pocketos-shell.c pocketos-audio-formats.h pocketos-files.h pocketos-radio-ui.h pocketos-update-ui.h pocketos-power.h pocketos-transfer-ui.h $(BUILD_DIR)/pocketos-reader-font.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) -I"$(BUILD_DIR)" $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-wifi: pocketos-wifi.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-bluetooth: pocketos-bluetooth.sh
	mkdir -p "$(BUILD_DIR)"
	cp "$<" "$@"
	chmod 755 "$@"

$(BUILD_DIR)/pocketos-radio-observe: pocketos-radio-observe.sh
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

$(BUILD_DIR)/pocketos-audio-monitor: pocketos-audio-monitor.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-transfer: pocketos-transfer.c
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
	$(CC) $(CPPFLAGS) $(CFLAGS) -o "$(BUILD_DIR)/test-radio-idle" test-radio-idle.c
	"$(BUILD_DIR)/test-radio-idle"
	rm -f "$(BUILD_DIR)/test-radio-idle"
	$(CC) $(CPPFLAGS) $(CFLAGS) -o "$(BUILD_DIR)/test-audio-route" test-audio-route.c
	"$(BUILD_DIR)/test-audio-route"
	rm -f "$(BUILD_DIR)/test-audio-route"
	test "$$($(BUILD_DIR)/pocketos-info --version)" = "PocketOS application SDK smoke test v1"
	sh test-r1-runtime.sh "$(abspath $(BUILD_DIR)/pocketos-r1-runtime)"
	sh test-shell.sh "$(abspath $(BUILD_DIR)/pocketos-shell)"
	python3 test-wifi.py "$(abspath $(BUILD_DIR)/pocketos-wifi)"
	sh -n pocketos-bluetooth.sh
	sh test-radio-observe.sh "$(abspath $(BUILD_DIR)/pocketos-radio-observe)"
	$(CC) $(CPPFLAGS) $(CFLAGS) -DPOCKETOS_TEST_FIXTURE -o "$(BUILD_DIR)/test-transfer-server" pocketos-transfer.c
	python3 test-transfer.py "$(abspath $(BUILD_DIR)/test-transfer-server)"
	rm -f "$(BUILD_DIR)/test-transfer-server"
	sh test-update.sh "$(abspath $(BUILD_DIR)/pocketos-update)" pack-update.py
	python3 test-music.py "$(abspath $(BUILD_DIR)/pocketos-music)"
	python3 test-audio-monitor.py "$(abspath $(BUILD_DIR)/pocketos-audio-monitor)"
	python3 test-books.py prepare-books.py
	sh test-decoder.sh "$(abspath $(BUILD_DIR)/pocketos-decoder)"
	sh test-start.sh "$(abspath $(BUILD_DIR)/pocketos-start)"
	sh -n "$(BUILD_DIR)/S95pocketos"
