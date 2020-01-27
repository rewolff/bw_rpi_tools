/*
 * bw_dmx.c. 
 *
 * Control the BitWizard SPI and I2C expansion boards on
 * Linux computers with spidev or i2c-dev. 
 *
 * based on: 
 * SPI testing utility (using spidev driver)
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 * Copyright (c) 2012-2013  Roger Wolff <R.E.Wolff@BitWizard.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
 *
 * Compile on raspberry pi with 
 *
 * gcc -Wall -O2 bw_dmx.c -o bw_dmx
 *
 * or with the included Makefile (type "make"). 
 */


#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>


#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <linux/i2c-dev.h>

#include "dmx.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))



struct spi_txrx spibuf;
static enum mode_t dmxmode = DMX_TX;

enum {SPI_MODE = 1, I2C_MODE, USB_I2CMODE, USB_SPIMODE }; 
static int mode = SPI_MODE;

static const char *device = "/dev/spidev0.0";
static uint8_t spi_mode;
static uint8_t bits = 8;
static uint32_t speed = 6000000;
static int delay = 0;
static int wait = 22000;
//static int addr = 0x82;
//static int text = 0;
//static char *monitor_file;
//static int readmode = 0;

//static int reg = -1;
//static long long val = -1;
//static int cls = 0;
//static int write8mode, writemiscmode, ident, readee;
//static int scan = 0;
//static int hexmode = 0;
//static char numberformat = 'x';

static int debug = 0;
#define DEBUG_REGSETTING 0x0001
#define DEBUG_TRANSFER   0x0002

static void pabort(const char *s)
{
  perror(s);
  exit(1);
}

void dump_buffer (unsigned char *buf, int n)
{
  int i;
  for (i=0;i<n;i++) 
    printf (" %02x", buf[i]);
}

void dump_buf (char *t, unsigned char *buf, int n)
{
  printf ("%s", t);
  dump_buffer (buf, n);
  printf ("\n");
}



static void spi_txrx (int fd, unsigned char *buf, int tlen, int rlen)
{
  int ret;
  struct spi_ioc_transfer tr = {
    .delay_usecs = delay,
    .speed_hz = speed,
    .bits_per_word = bits,
  };
  //printf ("txrx: buf=%p\n", buf);
  // if (rlen > tlen) tr.len = rlen; 
  // else             tr.len = tlen;
  tr.len = tlen + rlen;
  tr.tx_buf = (unsigned long) buf; 
  tr.rx_buf = (unsigned long) buf; 
  
  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1)
    pabort("can't send spi message");

}


static void i2c_txrx (int fd, unsigned char *buf, int tlen, int rlen)
{
   static int slave = -1;

   if (buf[0] != slave) {
      if (ioctl(fd, I2C_SLAVE, buf[0] >> 1) < 0) 
         pabort ("can't set slave addr");
      slave = buf[0];
   }
   if (write (fd, buf+1, tlen-1) != (tlen-1)) {
     pabort ("can't write i2c");
   }
   //XXX: check return code. 
   if (rlen) 
     if (read (fd, buf+tlen, rlen) != rlen) 
        pabort ("can't read i2c");
}


int myread (int fd, unsigned char *buf, int len)
{
  int nr, cr;

  nr = 0;
  while (nr < len) {
    cr = read (fd,  buf+nr, len-nr);
    if (cr < 0) return nr?nr:cr;
    nr += cr;
  }
  return nr;
}


static void usb_spitxrx (int fd, unsigned char *buf, int tlen, int rlen)
{
  //   static int slave = -1;
   static char cmd[] = {0x01, 0x10, 0 };

   cmd[2] = tlen + rlen; 

   if (write (fd, cmd, 3) != 3) {
     pabort ("can't write USB cmd");
   }
   if (write (fd, buf, tlen+rlen) != tlen+rlen) {
     pabort ("can't write USB buf");
   }

   //XXX: check return code. 
   if (myread (fd, buf, 2) != 2) {
     pabort ("can't read USB");
   }

   if (buf[0] != 0x90)
     pabort ("invalid response code from USB");

   if (buf[1] != tlen+rlen)
     pabort ("invalid lenght code from USB");

   if (myread (fd, buf, tlen+rlen) != tlen+rlen) 
     pabort ("can't read USB");
}



static void usb_i2ctxrx (int fd, unsigned char *buf, int tlen, int rlen)
{
  //   static int slave = -1;
  static char cmd[] = {1, 2, 0,0 };
  
  cmd[1] = 2; // I2C txrx
  cmd[2] = tlen+1;
  cmd[3] = rlen;
  
  if (write (fd, cmd, 4) != 4) {
    pabort ("can't write USB cmd");
  }
  if (write (fd, buf, tlen) != tlen) {
    pabort ("can't write USB buf");
  }

  if (myread (fd, buf, 3) != 3) {
    pabort ("can't read USB");
  }

  //  printf ("buf[] = %02x %02x %02x\n", buf[0], buf[1], buf[2]);

  if (buf[0] != 0x82)
    pabort ("invalid response code from USB");

  if (buf[1] != rlen+1)
    pabort ("i2c rlen incorrect");

  if (buf[2] != 0)
    pabort ("i2c transaction failed");

  if (myread (fd, buf+tlen, rlen) != rlen) 
    pabort ("can't read USB");
}


static void transfer(int fd, unsigned char *buf, int tlen, int rlen)
{
  //printf ("buf=%p.\n", buf);
  if (debug & DEBUG_TRANSFER) 
    dump_buf ("Before tx:", buf, tlen);
  if (mode == SPI_MODE) 
    spi_txrx (fd, buf, tlen, rlen);
  else if (mode == I2C_MODE) 
    i2c_txrx (fd, buf, tlen, rlen);
  else if (mode == USB_SPIMODE)
    usb_spitxrx (fd, buf, tlen, rlen);
  else if (mode == USB_I2CMODE)
    usb_i2ctxrx (fd, buf, tlen, rlen);
  else 
    pabort ("invalid mode...\n");

  if (debug & DEBUG_TRANSFER) 
    dump_buf ("rx:", buf, tlen+rlen);
}


#if 0
static void send_text (int fd, unsigned char *str) 
{
  unsigned char *buf; 
  int l;

  l = strlen ((char*)str);
  buf = malloc (l + 5); 
  buf[0] = addr;
  if (reg <= 0)
    buf [1] = 0; 
  else {
    buf [1] = reg; 
    l++;
  }
  strcpy ((char *)buf+2, (char*)str); 
  strcat ((char *)buf+2, "\xff\0"); // always append the 0xff, but it won't be sent if reg = 0;
  transfer (fd, buf, l+2, 0);
  free (buf);
}


static void set_reg_value8 (int fd, int reg, int val)
{
  unsigned char buf[5]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  transfer (fd, buf, 3, 0);
}



static void set_reg_value16 (int fd, int reg, int val)
{
  unsigned char buf[5]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  buf[3] = val >> 8;
  transfer (fd, buf, 4, 0);
}

static void set_reg_value32 (int fd, int reg, int val)
{
  unsigned char buf[15]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  buf[3] = val >> 8;
  buf[4] = val >> 16;
  buf[5] = val >> 24;
  transfer (fd, buf, 6, 0);
}


static void set_reg_value64 (int fd, int reg, long long val)
{
  unsigned char buf[15]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  buf[3] = val >> 8;
  buf[4] = val >> 16;
  buf[5] = val >> 24;
  buf[6] = val >> 32;
  buf[7] = val >> 40;
  buf[8] = val >> 48;
  buf[9] = val >> 56;
  transfer (fd, buf, 10, 0);
}


static unsigned int get_reg_value8 (int fd, int reg)
{
  unsigned char buf[5]; 

  buf[0] = addr | 1;
  buf[1] = reg;
  transfer (fd, buf, 2, 1);
  //dump_buffer (buf, 5);
  return buf[2];
}


static unsigned int get_reg_value16 (int fd, int reg)
{
  unsigned char buf[5]; 

  buf[0] = addr | 1;
  buf[1] = reg;
  transfer (fd, buf, 2, 2);
  //dump_buffer (buf, 5);
  return buf[2] | (buf[3] << 8);
}


static unsigned int get_reg_value32 (int fd, int reg)
{
  unsigned char buf[10]; 

  buf[0] = addr | 1;
  buf[1] = reg;
  transfer (fd, buf, 2, 4);
  //dump_buffer (buf, 5);
  return buf[2] | (buf[3] << 8) | (buf[4] << 16) | (buf[5] << 24);
}

static long long get_reg_value64 (int fd, int reg)
{
  unsigned char buf[10]; 
  unsigned int t, tt; 

  buf[0] = addr | 1;
  buf[1] = reg;
  transfer (fd, buf, 2, 8);
  t  = buf[2] | (buf[3] << 8) | (buf[4] << 16) | (buf[5] << 24);
  tt = buf[6] | (buf[7] << 8) | (buf[8] << 16) | (buf[9] << 24);
  return ((long long) tt << 32)  | t;
}


static void do_ident (int fd)
{
  unsigned char buf[0x20];
  int i;

  buf [0] = addr | 1;
  buf [1] = 1;

  transfer (fd, buf, 0x2,0x20);

  for (i= 2 ;i<0x20;i++) {
    if (!buf[i]) break;
    putchar (buf[i]);
  }
  putchar ('\n');
}


static void do_readee (int fd)
{
#define EELEN 0x80
  unsigned char buf[EELEN];
  int i;

  buf [0] = addr | 1;
  buf [1] = 2;

  transfer (fd, buf, 0x2, 0x80);

  for (i = 0;i < EELEN;i++) {
    if (!(i & 0xf)) printf ("\n%04x:  ", i); 
    printf ("%02x ", ((unsigned char *) buf)[i+2]);

  }
  printf ("\n");
}


#endif

char mkprintable (char ch)
{
  if (ch < ' ') return '.';
  if (ch <= '~') return ch;
  return '.';
}


#if 0

static void do_scan (int fd)
{
  unsigned char buf[0x20];
  int add;
  int i;

  for (add = 0;add < 255;add += 2) {
    buf[0] = add | 1;
    buf[1] = 1;
    transfer (fd, buf, 0x2, 0x20);
    for (i=(mode==I2C_MODE)?0:2;i<0x20;i++) {
      if (mkprintable (buf[i]) != '.') break;
    }
    if (i != 0x20) {
      printf ("%02x: ", add);
      for (i=(mode==I2C_MODE)?0:2;i<0x20;i++) {
	if (buf[i] == 0) break;
	putchar (mkprintable (buf[i]));
      }
      printf ("\n");
    }
  }

}

#endif


static void print_usage(const char *prog)
{
  printf("Usage: %s [-DsbdlHOLC3]\n", prog);
  puts("  -D --device   device to use (default /dev/spidev1.1)\n"
       "  -s --speed    max speed (Hz)\n"
       "  -d --delay    delay (usec)\n"
       "  -r --rx      \n"
       "  -v --val      value\n"
       "  -a --addr     address\n"
       "  -w --write8   write an octet\n"
       "  -W --write    write arbitrary type\n"
       "  -i --interval set the interval\n"
       "  -S --scan     Scan the bus for devices \n"
       "  -R --read     multi-datasize read\n"
       "  -I --i2c      I2C mode (uses /dev/i2c-0, change with -D)\n"
       "  -U --usb      USB mode (uses /dev/ttyACM0, change with -D)\n"
       "  -1 --decimal  Numbers are decimal. (registers remain in hex)\n"
  );

  exit(1);
}

static const struct option lopts[] = {

  // SPI options. 
  { "device",  1, 0, 'D' },
  { "speed",   1, 0, 's' },
  { "delay",   1, 0, 'd' },
  { "wait",    1, 0, 'w' },

  { "idle",      0, 0, 'i' },
  { "rx",        0, 0, 'r' },
  // { "addr",      1, 0, 'a' },
  // { "write8",    0, 0, 'w' },
  // { "write",     0, 0, 'W' },
  //{ "interval",  1, 0, 'i' },
  //{ "scan",      0, 0, 'S' },
  //{ "read",      0, 0, 'R' },
  //{ "eeprom",    0, 0, 'e' },


  // Options for LCD
  //{ "text",      0, 0, 't' },
  //{ "cls",       0, 0, 'C' },
  //{ "monitor",   1, 0, 'm' },

  //{ "hex",       0, 0, 'h' },

  //{ "i2c",       0, 0, 'I' },
  //{ "usbspi",    0, 0, 'u' },
  //{ "usbi2c",    0, 0, 'U' },
  //{ "decimal",   0, 0, '1' },

  { "verbose",   1, 0, 'V' },
  { "help",      0, 0, '?' },
  { NULL, 0, 0, 0 },
};



static int parse_opts(int argc, char *argv[])
{
  while (1) {
    int c;

    c = getopt_long(argc, argv, "D:s:d:rV:w:i", lopts, NULL);

    if (c == -1)
      break;

    switch (c) {
    case 'D':
      device = strdup (optarg);
      if (strstr (device, "i2c")) mode=I2C_MODE;
      else                        mode=SPI_MODE;
      break;
    case 's':
      speed = atoi(optarg);
      break;
    case 'd':
      delay = atoi(optarg);
      break;
    case 'w':
      wait = 1000*atoi(optarg);
      break;
    case 'r':
      dmxmode = DMX_RX;
      break;
    case 'V':
      debug = atoi(optarg);
      break;
    case 'i':
      dmxmode = DMX_IDLE;
      break;

    case '?':
      print_usage (argv[0]);
      exit (0);
      break;

    default:
      print_usage(argv[0]);
      break;
    }
  }
  return optind; 
}


void setup_spi_mode (int fd)
{
  int ret;
  /*
   * spi mode
   */
  // printf ("setting spi mode. \n");
  ret = ioctl(fd, SPI_IOC_WR_MODE, &spi_mode);
  if (ret == -1)
    pabort("can't set spi mode");

  ret = ioctl(fd, SPI_IOC_RD_MODE, &spi_mode);
  if (ret == -1)
    pabort("can't get spi mode");

  /*
   * bits per word
   */
  ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
  if (ret == -1)
    pabort("can't set bits per word");

  ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
  if (ret == -1)
    pabort("can't get bits per word");

  /*
   * max speed hz
   */
  ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
  if (ret == -1)
    pabort("can't set max speed hz");

  ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
  if (ret == -1)
    pabort("can't get max speed hz");

  //printf("spi mode: %d\n", spi_mode);
  //printf("bits per word: %d\n", bits);
  //printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);
}



#if 0
void wait_for_file_changed (char *fname)
{
  static time_t lastmtime = 0;
  struct stat statb;

  while (1) {
    if (stat (fname, &statb) < 0) {
      pabort (fname);
    }

    if (lastmtime != statb.st_mtime) {
      //printf ("fc: %x / %x\n", (int) lastmtime, (int) statb.st_mtime);
      lastmtime = statb.st_mtime;
      return;
    }
    usleep (250000);
  }
}


unsigned char *get_file_line (char *fname, int lno)
{
  static unsigned char buf[0x50], *p;
  FILE *f;
  int i;


  f = fopen (fname, "r");
  if (!f) pabort (fname);
  for (i = 0;i<=lno;i++) {
    buf [0] = 0;
    p = (unsigned char*)fgets ((char*)buf, 0x3f, f);
    if (!p) return p;
  }

  fclose (f);
  buf[strlen((char*)buf)-1] = 0; // chop!
  return buf;
}


void do_monitor_file (int fd, char *fname)
{
  int i;
  unsigned char *buf;
  char olddisplay[4][0x20];

  //fprintf (stderr, "monitoring %s on fd %d.\n", fname, fd);
  while (1) {
    wait_for_file_changed (fname);
    //set_reg_value8 (fd, 0x10, 0xaa);
    for (i=0;i<4;i++) {
      buf = get_file_line (fname, i);
      if (!buf) break;
      while (strlen ((char*)buf) < 20) strcat ((char*)buf, "    ");
      buf[20] = 0;
      if (strcmp ((char*)buf, olddisplay[i])) { 
        strcpy (olddisplay[i], (char *)buf);
        set_reg_value8 (fd, 0x11, i<<5);
        send_text (fd, buf);
        usleep (50000);
      }
    }
  }
}

#endif



#define MAXUNIV 16

int main(int argc, char *argv[])
{
  int fd;
  int nonoptions;
  int last; 
//  int i, rv;
//  char typech;
//  char format[32];
  char *thefile;
  int infd;
  unsigned char *data[MAXUNIV];
  int nodata = 0;
  int i, u, numuniv;

  if (argc <= 1) {
    print_usage (argv[0]);
    exit (0);
  }

  nonoptions = parse_opts(argc, argv);

  //fprintf (stderr, "dev = %s\n", device);
  //fprintf (stderr, "mode = %d\n", mode);
  fd = open(device, O_RDWR);
  if (fd < 0)
    pabort("can't open device");

  if (mode == SPI_MODE) setup_spi_mode (fd);

  if (dmxmode == DMX_IDLE) {
    spibuf.cmd = CMD_IDLE;
    transfer (fd, (void*) &spibuf, 0x209, 0); 
    exit (0);
  }   

  numuniv = 0;
  for (i=nonoptions;i<argc;i++, numuniv++) {
    thefile = argv[i];
    infd = open (thefile, O_RDWR); 
    if (infd < 0) {
      perror (thefile);
      exit (1);
    }
    data[numuniv] = mmap (NULL, 0x200, PROT_READ | PROT_WRITE, MAP_SHARED, infd, 0);
    if (data[numuniv] == (void *)-1) {
      perror ("mmap");
      exit (1);
    }
  }
  //printf ("got %d unvi.\n", numuniv);
  last = -1;
  u = 0;
  while (1) {
    if (dmxmode == DMX_TX) {
      //putchar ('0'+u); fflush (stdout);
      spibuf.cmd = CMD_DMX_DATA;
      spibuf.p1 = 0x1 | (u << 10);
      spibuf.p2 = 0x200;
      memcpy (spibuf.dmxbuf, data[u++], 0x200);
      if (u >= numuniv) u = 0;
    }

    if (dmxmode == DMX_RX) {
       spibuf.cmd = CMD_READ_DMX;
    }

    // transfer 8 byte header + 513 byte datablock. 
    transfer (fd, (void*) &spibuf, 0x209, 0); 

    if (dmxmode == DMX_RX) {
       if (spibuf.p1 != last) {
          memcpy (data[0], spibuf.dmxbuf, 0x200);
          last = spibuf.p1;
       } else {
          if (spibuf.cmd == STAT_RX_IN_PROGRESS) {
             usleep (1000);
             continue;
          }
          printf ("no data %d\r", nodata++); 
          fflush (stdout);
       }
    }
    usleep (wait);
  }

  exit (0);
}
