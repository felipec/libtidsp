DSP_API := 1
SN_API := 2

dspdir := /lib/dsp
prefix := /usr

CC := gcc

CPPFLAGS := -ggdb -Wall -Wextra -Wno-unused-parameter
LDFLAGS := -Wl,--no-undefined -Wl,--as-needed

override CFLAGS += -std=c99 -D_GNU_SOURCE
override CFLAGS += -DDSP_API=$(DSP_API) -DSN_API=$(SN_API) -D DSP_DIR='"$(dspdir)/"'

prefix := /usr
libdir := $(prefix)/lib
version := $(shell ./get-version)

all:

libtidsp.so: dsp_bridge.o log.o tidsp.o codecs/td_mp4vdec.o
libtidsp.so: override CPPFLAGS += -I. -fPIC
libtidsp.so: override LDFLAGS += -Wl,-soname,libtidsp.so.0

all: libtidsp.so

libtidsp.pc: libtidsp.pc.in
	sed -e 's#@prefix@#$(prefix)#g' \
		-e 's#@version@#$(version)#g' \
		-e 's#@libdir@#$(libdir)#g' $< > $@

D = $(DESTDIR)

install: libtidsp.so libtidsp.pc
	install -m 755 -D libtidsp.so $(D)$(libdir)/libtidsp.so.0
	ln -sf libtidsp.so.0 $(D)$(libdir)/libtidsp.so
	install -m 644 -D tidsp.h $(D)$(prefix)/include/tidsp.h
	install -m 644 -D libtidsp.pc $(D)$(libdir)/pkgconfig/libtidsp.pc

# pretty print
ifndef V
QUIET_CC    = @echo '   CC         '$@;
QUIET_LINK  = @echo '   LINK       '$@;
QUIET_CLEAN = @echo '   CLEAN      '$@;
endif

%.so::
	$(QUIET_LINK)$(CC) $(LDFLAGS) -shared $^ $(LIBS) -o $@

%.o:: %.c
	$(QUIET_CC)$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -o $@ -c $<

clean:
	$(QUIET_CLEAN)$(RM) libtidsp.so $(binaries) libtidsp.pc \
		`find -name '*.[oad]'`

-include *.d
