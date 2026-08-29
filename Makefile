CC ?= clang
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Icore/include -Isrc
LDFLAGS += -framework IOKit -framework CoreFoundation -framework ApplicationServices

BIN := build/macOS-trackpoint-scroll
OBJ := \
	build/mac_trackpoint_scroll.o \
	build/config.o \
	build/event_shim.o \
	build/modifier_modes.o \
	build/engine.o \
	build/profiles.o

.PHONY: all clean check-submodule

all: check-submodule $(BIN)

check-submodule:
	@test -f core/src/engine.c || { echo "core submodule missing; run: git submodule update --init --recursive" >&2; exit 1; }

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

build/mac_trackpoint_scroll.o: src/mac_trackpoint_scroll.c src/config.h src/event_shim.h src/modifier_interpose.h src/modifier_modes.h
	@mkdir -p build
	$(CC) $(CFLAGS) -include src/event_shim.h -include src/modifier_interpose.h -c $< -o $@

build/config.o: src/config.c src/config.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/event_shim.o: src/event_shim.c src/event_shim.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/modifier_modes.o: src/modifier_modes.c src/modifier_modes.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/engine.o: core/src/engine.c core/include/trackpoint_scroll/engine.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/profiles.o: core/src/profiles.c core/include/trackpoint_scroll/profiles.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build
