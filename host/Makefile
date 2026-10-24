# Programas de PC com o nucleo do firmware:
#   make            -> pc_bridge (Linux/macOS) e sim_serial (E3634A virtual, Linux)
#   make windows    -> pc_bridge.exe (precisa do MinGW: x86_64-w64-mingw32-gcc)
CC ?= cc
CFLAGS ?= -std=c11 -D_DEFAULT_SOURCE -O2 -Wall -Wextra -Wno-unused-parameter
CORE = ../firmware/core
CORE_SRC = $(CORE)/psu.c $(CORE)/bridge.c $(CORE)/json.c
DEPS = $(CORE_SRC) $(wildcard $(CORE)/*.h)

all: pc_bridge sim_serial

pc_bridge: pc_bridge.c $(DEPS)
	$(CC) $(CFLAGS) -I$(CORE) -o $@ pc_bridge.c $(CORE_SRC) -lm

sim_serial: sim_serial.c ../tests/sim/sim_e3634a.c ../tests/sim/sim_e3634a.h
	$(CC) $(CFLAGS) -Wno-format-truncation -I../tests -o $@ sim_serial.c ../tests/sim/sim_e3634a.c -lm

windows: bin/pc_bridge.exe

bin/pc_bridge.exe: pc_bridge.c $(DEPS)
	mkdir -p bin
	x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wno-unused-parameter -I$(CORE) -static -s -o $@ pc_bridge.c $(CORE_SRC)

clean:
	rm -f pc_bridge sim_serial

.PHONY: all windows clean
