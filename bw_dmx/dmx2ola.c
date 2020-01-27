/*
 * dmx2ola.c. 
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
 * gcc -Wall -O2 dmx2ola.c -o dmx2ola
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
//static enum mode_t dmxmode = DMX_TX;

enum {SPI_MODE = 1, I2C_MODE, USB_I2CMODE, USB_SPIMODE }; 
static int mode = SPI_MODE;

static const char *device = "/dev/spidev0.0";
static uint8_t spi_mode;
static uint8_t bits = 8;

static uint32_t speed = 6000000;
static int delay = 0;
static int wait = 22000;
static int universe = 0;


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

  { "universe",  1, 0, 'u' },

  { "help",      0, 0, '?' },
  { NULL, 0, 0, 0 },
};



static int parse_opts(int argc, char *argv[])
{
  while (1) {
    int c;

    c = getopt_long(argc, argv, "D:s:d:u:?", lopts, NULL);

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
    case 'u':
      universe = atoi(optarg);
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



int main(int argc, char *argv[])
{
  int fd;
  //int nonoptions;
  int last; 
  int i;
  char ola_streaming_cmd[0x80];
  FILE *ola_streaming_fp;
  //int infd;
  unsigned char data[0x210];
  int nodata = 0;
  int yesdata = 0;

#if 0
  if (argc <= 1) {
    print_usage (argv[0]);
    exit (0);
  }
#endif

  //  nonoptions = 
  parse_opts(argc, argv);

  //fprintf (stderr, "dev = %s\n", device);
  //fprintf (stderr, "mode = %d\n", mode);

  fd = open(device, O_RDWR);
  if (fd < 0)
    pabort("can't open device");

  if (mode == SPI_MODE) setup_spi_mode (fd);

  sprintf (ola_streaming_cmd, "ola_streaming_client -u %d", universe);
  ola_streaming_fp = popen (ola_streaming_cmd, "w");
  if (ola_streaming_fp == NULL ) {
    perror ("opening pipe to ola_streaming_client"); 
    exit (1);
  }
  
  last = -1;
  while (1) {
    spibuf.cmd = CMD_READ_DMX;

    // transfer 8 byte header + 513 byte datablock. 
    transfer (fd, (void*) &spibuf, 0x209, 0); 

    if (spibuf.p1 != last) {
      if (memcmp (data, spibuf.dmxbuf, 0x200) != 0) {
	memcpy (data, spibuf.dmxbuf, 0x200);
	// The DMX data starts at offset 1. 
	for (i=1;i<512;i++)
	  fprintf (ola_streaming_fp, "%d,", data[i]);
	//fprintf (ola_streaming_fp, "%d\n", data[i]);
	fflush (ola_streaming_fp);
      }
      last = spibuf.p1;
      printf ("data/nodata %d/%d \r", yesdata++, nodata); 
      fflush (stdout);
    } else {
      if (spibuf.cmd == STAT_RX_IN_PROGRESS) {
	usleep (1000);
	continue;
      }
      printf ("data/nodata %d/%d \r", yesdata, nodata++); 
      fflush (stdout);
    }
    
    usleep (wait);
  }
  

  exit (0);
}
