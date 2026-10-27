# Testes do firmware C no PC:  make -C tests
CC ?= cc
CFLAGS ?= -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Wno-format-truncation -O1 -g -fsanitize=address,undefined
CORE = ../firmware/core
SRC = test_core.c sim/sim_e3634a.c $(CORE)/psu.c $(CORE)/bridge.c $(CORE)/json.c

all: test_core
	./test_core

test_core: $(SRC) $(wildcard $(CORE)/*.h) sim/sim_e3634a.h
	$(CC) $(CFLAGS) -I$(CORE) -I. -o $@ $(SRC) -lm

clean:
	rm -f test_core

.PHONY: all clean e2e

# ponta a ponta com processos e porta serial virtual (Linux)
e2e:
	python3 e2e_serial.py
