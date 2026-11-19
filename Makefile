# CampusCompute - OS Level 3 Mini-Project (C, Linux / WSL Ubuntu)
#
#   make            build ./campuscompute
#   make test       build and run the automated smoke tests
#   make clean      remove the executable and object files
#   make distclean  clean + remove generated logs
#   make dist SRN=<your full SRN>
#                   create ../<SRN>.zip with the source and build files

CC       = gcc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g
CFLAGS  += -Iinclude -D_GNU_SOURCE
LDLIBS  := -pthread -lrt

TARGET  := campuscompute
SRC     := $(wildcard src/*.c)
OBJ     := $(patsubst src/%.c,build/%.o,$(SRC))
HDR     := $(wildcard include/*.h)
PROJECT := $(notdir $(CURDIR))

.PHONY: all test clean distclean dist

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c $(HDR) | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

test: $(TARGET)
	bash tests/run_tests.sh

clean:
	rm -rf build $(TARGET)

distclean: clean
	rm -rf logs

dist:
	@test -n "$(SRN)" || { echo "Usage: make dist SRN=<your full SRN>"; exit 1; }
	cd .. && rm -f "$(SRN).zip" && zip -r "$(SRN).zip" "$(PROJECT)" \
	    -x "$(PROJECT)/build/*" "$(PROJECT)/logs/*" "$(PROJECT)/$(TARGET)"
	@echo "Created ../$(SRN).zip"
