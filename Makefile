CC ?= gcc
CFLAGS ?= -O2 -fPIC -Wall -Wextra

libVkLayer_vram_overflow.so: vram_overflow.c
	$(CC) -shared $(CFLAGS) -o $@ $<

clean:
	rm -f libVkLayer_vram_overflow.so
