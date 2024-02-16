VERSION=0
REVISION=7
BUILDDATE="unreleased"

CROSS=powerpc64-linux-gnu-
CC=$(CROSS)gcc
LD=$(CROSS)ld
STRIP=$(CROSS)strip

ARCH_CFLAGS=-m32 -mbig-endian -mcpu=G3
EXTRA_CFLAGS=-fomit-frame-pointer -fno-stack-protector
EXTRA_CFLAGS+=-fno-asynchronous-unwind-tables -fno-unwind-tables
# strcmp and strncmp builtins don't seem to work
EXTRA_CFLAGS+=-fno-builtin-strcmp -fno-builtin-strncmp

CFLAGS=-Wall -Wextra -O2 -g $(ARCH_CFLAGS) $(EXTRA_CFLAGS)
ASFLAGS=$(ARCH_CFLAGS)
CPPFLAGS=-Iinclude -DVERSION=$(VERSION) -DREVISION=$(REVISION) -DBUILDDATE='$(BUILDDATE)'
LDFLAGS=-nostdlib -e_start -static -Wl,-n,-Tbboot.lds,--build-id=none
LDFLAGS+=-Wl,--gc-sections,--orphan-handling=discard
LDLIBS=-L$(shell $(CC) -print-file-name=32) -lgcc

DIRS=drivers include libc zip

EXTRA_DIST=\
  COPYING \
  README.rst \
  Makefile \
  bboot.fth \
  bboot.lds

LIBC_OBJS=\
  libc/entry.o \
  libc/printf.o \
  libc/setjmp.o \
  libc/string.o \
  libc/string_ppc.o

SOURCES=\
  bboot.c \
  boot_aos.c \
  brd_amigaone.c \
  brd_pegasos2.c \
  cfg.c \
  drivers/console.c \
  drivers/uart8250mem.c \
  drivers/pci.c \
  drivers/prom.c \
  zip/puff.c \
  zip/zip.c

bboot: $(SOURCES:.c=.o) $(LIBC_OBJS)

Depend: Makefile $(SOURCES)
	$(CC) -MM $(CPPFLAGS) $(SOURCES) >Depend

include Depend

.PHONY: strip clean distclean dist

strip: bboot
	$(STRIP) $<

clean:
	rm -f *~ */*~
	rm -f $(LIBC_OBJS)
	rm -f $(SOURCES:.c=.o)

distclean: clean
	rm -f Depend
	rm -f bboot

dist: TARNAME=bboot-$(VERSION).$(REVISION)
dist: TOPSRC=$(shell echo $(SOURCES).| sed -e 's|[^ ]*/[^ ]*||g')
dist: BUILDDATE=$(shell date "+%e,%m.%Y" | sed -e 's/ *//' -e 's/,0/,/' -e 's/,/./' -e 's/\(.*\)/"\1"/')
dist:
	mkdir /tmp/$(TARNAME)
	tar -cf - $(EXTRA_DIST) $(TOPSRC) $(DIRS) | tar -C /tmp/$(TARNAME) -xf -
	$(MAKE) -C /tmp/$(TARNAME) BUILDDATE='$(BUILDDATE)' strip clean
	tar -C /tmp --group=root --owner=root -Jcf $(TARNAME).tar.xz $(TARNAME)
	rm  -rf /tmp/$(TARNAME)
