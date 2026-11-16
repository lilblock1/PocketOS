CC ?= cc
BUILD_DIR ?= ../app-output
CFLAGS ?= -Os -Wall -Wextra -Werror
.PHONY: all check
all: $(BUILD_DIR)/pocketos-info

$(BUILD_DIR)/pocketos-info: pocketos-info.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o "$@" "$<" $(LDLIBS)

check: all
	test "$$($(BUILD_DIR)/pocketos-info --version)" = "PocketOS application SDK smoke test v1"
