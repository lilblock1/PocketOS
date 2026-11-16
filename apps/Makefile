CC ?= cc
BUILD_DIR ?= ../app-output
CFLAGS ?= -Os -Wall -Wextra -Werror
.PHONY: all check
all: $(BUILD_DIR)/pocketos-info $(BUILD_DIR)/pocketos-r1-runtime $(BUILD_DIR)/pocketos-shell

$(BUILD_DIR)/pocketos-info: pocketos-info.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-r1-runtime: pocketos-r1-runtime.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

$(BUILD_DIR)/pocketos-shell: pocketos-shell.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

check: all
	test "$$($(BUILD_DIR)/pocketos-info --version)" = "PocketOS application SDK smoke test v1"
	sh test-r1-runtime.sh "$(abspath $(BUILD_DIR)/pocketos-r1-runtime)"
	sh test-shell.sh "$(abspath $(BUILD_DIR)/pocketos-shell)"
