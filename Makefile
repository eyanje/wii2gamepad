CC=gcc

SRC_DIR=src
HEADERS=$(wildcard $(SRC_DIR)/*.h)
SOURCES=$(wildcard $(SRC_DIR)/*.c)
OBJS=$(addsuffix .o, $(basename $(SOURCES)))

EXEC=wii2gamepad

CFLAGS += \
	-I /usr/include/libevdev-1.0 \
	-g

LDFLAGS = \
	-L /usr/local/lib \
	-l evdev \
	-l xwiimote \

.PHONY: all

all: $(EXEC)

$(EXEC): $(HEADERS) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm -f $(EXEC) $(OBJS)
