CC      ?= gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -s -D_GNU_SOURCE -Iinclude
LDLIBS  = -lcurl

SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:src/%.c=obj/%.o)
TARGET  = bin/minagent

.PHONY: all static clean dirs

all: dirs $(TARGET)

dirs:
	mkdir -p obj bin

$(TARGET): $(OBJ) | dirs
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

obj/%.o: src/%.c | dirs
	$(CC) $(CFLAGS) -c -o $@ $<

static: CFLAGS  += -static
static: LDLIBS  += -static
static: dirs $(TARGET)

clean:
	rm -rf obj bin
