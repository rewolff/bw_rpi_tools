
CFLAGS=-Wall -O2
CC=gcc 

MYBIN=bw_dmx mon_dmx dmx2ola dmx_uart makechar set_output dmx_udp set_dmx dmx_random
all: $(MYBIN)

install: $(MYBIN)
	cp $(MYBIN) /usr/bin

clean:
	rm -f *~ *.o
