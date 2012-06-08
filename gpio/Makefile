
CC=gcc 
CFLAGS=-Wall -O2

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
BINARIES=gpio_list send_spi string_to_hex send_i2c
LINKS=gpio_clr gpio_get gpio_set gpio_setfunc
LINKS2=read_i2c
SCRIPTS=clrdisplay mon_display moveto writedisplay i2c_identify
all: $(BINARIES)

clean: 
	rm -f *.o $(BINARIES)

install:
	cp $(BINARIES) $(SCRIPTS) $(BINDIR)
	chmod 4755 $(BINDIR)/send_spi
	for i in $(LINKS) ; do \
	   rm -f  $(BINDIR)/$$i ; \
	   ln -s $(BINDIR)/gpio_list $(BINDIR)/$$i ; \
	done
	for i in $(LINKS2) ; do \
	   rm -f  $(BINDIR)/$$i ; \
	   ln -s $(BINDIR)/send_i2c $(BINDIR)/$$i ; \
	done
	
