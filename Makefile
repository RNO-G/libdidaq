#  Makefile for libdidaq
CC?=gcc
CFLAGS?=-Wall -O2

PREFIX?=/usr/local
LIBDIR?=lib
INCDIR?=include
BINDIR?=bin

LIB=libdidaq.so
VER_MAJOR=0
VER_MINOR=0
VER_REV=1


VERSIONED_LIB = $(LIB).$(VER_MAJOR).$(VER_MINOR).$(VER_REV)
NAMED_LIB = $(LIB).$(VER_MAJOR)

SRC=src/didaq_regs.c
OBJ = $(SRC:.c=.o)
INC_PUBLIC=src/didaq.h src/didaq_regs.h
INC_PRIVATE=src/didaq_internal.h src/didaq_helpers.h
INCLUDES=$(INC_PUBLIC) $(INC_PRIVATE)

.PHONY: all clean examples

all: $(LIB)

src/didaq.h: src/didaq.h.in
	sed $< -e "s/DIDAQ_VERSION_MAJOR @@/DIDAQ_VERSION_MAJOR $(VER_MAJOR)/" -e "s/DIDAQ_VERSION_MINOR @@/DIDAQ_VERSION_MINOR $(VER_MINOR)/" -e "s/DIDAQ_VERSION_REV @@/DIDAQ_VERSION_REV $(VER_REV)/" > $@

src/%.o: src/%.c $(INCLUDES)
	$(CC) $(CFLAGS) -fPIC -Isrc -c $< -o $@

examples/%: examples/%.c $(LIB)
	$(CC) $(CFLAGS)  -Iinclude $< -o $@ -L. -lgpios $(LDFLAGS)

$(LIB): $(NAMED_LIB)
	ln -sf  $< $@

$(NAMED_LIB): $(VERSIONED_LIB)
	ln -sf  $< $@

$(VERSIONED_LIB): $(OBJ)
	 $(CC) -shared $(LDFLAGS) -Wl,-soname,$@ -o $@ $^

install: $(LIB) examples
	install -d $(DESTDIR)$(PREFIX)/$(LIBDIR)
	install -m 0755 $(VERSIONED_LIB) $(DESTDIR)$(PREFIX)/$(LIBDIR)/
	ln -sfr $(DESTDIR)$(PREFIX)/$(LIBDIR)/$(VERSIONED_LIB) $(DESTDIR)$(PREFIX)/$(LIBDIR)/$(NAMED_LIB)
	ln -sfr $(DESTDIR)$(PREFIX)/$(LIBDIR)/$(NAMED_LIB) $(DESTDIR)$(PREFIX)/$(LIBDIR)/$(LIB)
	install -d $(DESTDIR)$(PREFIX)/$(INCDIR)
	install -m 0644 $(INC_PUBLIC) $(DESTDIR)$(PREFIX)/$(INCDIR)/
	install -d $(DESTDIR)$(PREFIX)/$(BINDIR)
#	install -m 0755 $(EXAMPLES) $(DESTDIR)$(PREFIX)/$(BINDIR)/



clean:
	rm -f $(LIB) $(OBJS) $(NAMED_LIB) $(VERSIONED_LIB)


