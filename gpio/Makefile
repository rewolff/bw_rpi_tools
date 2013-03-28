
CC=gcc 
CFLAGS=-Wall -O2

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin
BINARIES=gpio_list
LINKS=gpio_clr gpio_get gpio_set gpio_setfunc
SCRIPTS=clrdisplay mon_display moveto writedisplay i2c_identify
all: $(BINARIES)

clean: 
	rm -f *.o $(BINARIES)

install:
	cp $(BINARIES) $(SCRIPTS) $(BINDIR)
	for i in $(LINKS) ; do \
	   rm -f  $(BINDIR)/$$i ; \
	   ln -s $(BINDIR)/gpio_list $(BINDIR)/$$i ; \
	done
	
