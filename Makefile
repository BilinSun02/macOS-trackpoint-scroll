CC ?= clang
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Icore/include
LDFLAGS += -framework IOKit -framework CoreFoundation -framework ApplicationServices

BIN := build/macOS-trackpoint-scroll
SRC := src/mac_trackpoint_scroll.c core/src/engine.c core/src/profiles.c

.PHONY: all clean check-submodule

all: check-submodule $(BIN)

check-submodule:
	@test -f core/src/engine.c || { echo "core submodule missing; run: git submodule update --init --recursive" >&2; exit 1; }

$(BIN): $(SRC) core/include/trackpoint_scroll/engine.h core/include/trackpoint_scroll/profiles.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LDFLAGS)

clean:
	rm -rf build
