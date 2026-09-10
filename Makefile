CC ?= clang
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Icore/include -Isrc
LDFLAGS += -framework IOKit -framework CoreFoundation -framework ApplicationServices

BIN := build/macOS-trackpoint-scroll
HELPER_BIN := build/macOS-trackpoint-scroll-edge-pressure-helper

OBJ := \
	build/mac_trackpoint_scroll.o \
	build/config.o \
	build/edge_pressure_client.o \
	build/event_shim.o \
	build/pointer_rebound.o \
	build/pointer_hid_shim.o \
	build/engine.o \
	build/core_rebound.o \
	build/profiles.o

HELPER_OBJ := \
	build/edge_pressure_helper.o \
	build/karabiner_vhid.o

.PHONY: all clean check-submodule install-user uninstall-user dist

all: check-submodule $(BIN) $(HELPER_BIN)

check-submodule:
	@test -f core/src/engine.c || { echo "core submodule missing; run: git submodule update --init --recursive" >&2; exit 1; }

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(HELPER_BIN): $(HELPER_OBJ)
	$(CC) $(HELPER_OBJ) -o $@ -framework IOKit -framework CoreFoundation

build/mac_trackpoint_scroll.o: src/mac_trackpoint_scroll.c src/config.h src/event_shim.h src/edge_pressure_client.h src/pointer_rebound.h
	@mkdir -p build
	$(CC) $(CFLAGS) \
		-DIOHIDValueGetIntegerValue=tpsc_pointer_value_get_integer_value \
		-include src/event_shim.h -c $< -o $@

build/config.o: src/config.c src/config.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/edge_pressure_client.o: src/edge_pressure_client.c src/edge_pressure_client.h src/edge_pressure_protocol.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/event_shim.o: src/event_shim.c src/event_shim.h src/pointer_rebound.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/pointer_rebound.o: src/pointer_rebound.c src/pointer_rebound.h core/include/trackpoint_scroll/rebound.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/pointer_hid_shim.o: src/pointer_hid_shim.c src/config.h src/pointer_rebound.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/edge_pressure_helper.o: src/edge_pressure_helper.c src/edge_pressure_protocol.h src/karabiner_vhid.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/karabiner_vhid.o: src/karabiner_vhid.c src/karabiner_vhid.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/engine.o: core/src/engine.c core/include/trackpoint_scroll/engine.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/core_rebound.o: core/src/rebound.c core/include/trackpoint_scroll/rebound.h core/include/trackpoint_scroll/engine.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/profiles.o: core/src/profiles.c core/include/trackpoint_scroll/profiles.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

install-user: all
	sh scripts/install-user.sh

uninstall-user:
	sh scripts/uninstall-user.sh

dist: all
	sh scripts/package-release.sh

clean:
	rm -rf build
