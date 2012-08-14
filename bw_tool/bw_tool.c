/*
 * bw_tool.c. 
 *
 * Control the BitWizard SPI expansion boards. 
 *
 * based on: 
 * SPI testing utility (using spidev driver)
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
 *
 * Compile on raspberry pi with 
 *
 * gcc -Wall -O2 bw_tool.c -o bw_tool
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
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <linux/i2c-dev.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))


#define SPI_MODE 1
#define I2C_MODE 2
static const char *device = "/dev/spidev0.0";
static int mode = SPI_MODE;
static uint8_t spi_mode;
static uint8_t bits = 8;
static uint32_t speed = 450000;
static uint16_t delay = 2;
static int addr = 0x82;
static int text = 0;

static int reg = -1;
static int val = -1;
static int cls = 0;
static int write8mode, write16mode, ident;

static void pabort(const char *s)
{
  perror(s);
  abort();
}


static void spi_txrx (int fd, char *buf, int len)
{
  int ret;
  struct spi_ioc_transfer tr = {
    .delay_usecs = delay,
    .speed_hz = speed,
    .bits_per_word = bits,
  };

  tr.len = len; 
  tr.tx_buf = (unsigned long) buf; 
  tr.rx_buf = (unsigned long) buf; 
  
  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1)
    pabort("can't send spi message");

}

static void i2c_txrx (int fd, char *buf, int len)
{
   static int slave = -1;

   if (buf[0] != slave) {
      if (ioctl(fd, I2C_SLAVE, buf[0] >> 1) < 0) 
         pabort ("cant set slave addr");
      slave = buf[0];
   }
   write (fd, buf+1, len-1);
}


static void transfer(int fd, char *buf, int len)
{
  if (mode == SPI_MODE) 
     spi_txrx (fd, buf, len);
  else
     i2c_txrx (fd, buf, len);
}


static void send_text (int fd, char *str) 
{
  char *buf; 
  int l;

  l = strlen (str);
  buf = malloc (l + 5); 
  buf[0] = addr;
  buf[1] = 0; 
  strcpy (buf+2, str); 
  transfer (fd, buf, l+2);
  free (buf);
}


static void set_reg_value8 (int fd, int reg, int val)
{
  char buf[5]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  transfer (fd, buf, 3);
}


static void set_reg_value16 (int fd, int reg, int val)
{
  char buf[5]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  buf[3] = val >> 8;
  transfer (fd, buf, 4);
}


static void do_ident (int fd)
{
  char buf[0x20];
  int i;

  buf [0] = addr | 1;
  buf [1] = 1;

  // XXX allow I2C version to ident too!
  spi_txrx (fd, buf, 0x20);

  for (i=2;i<0x20;i++) {
    if (!buf[i]) break;
    putchar (buf[i]);
  }
  putchar ('\n');
}


static void print_usage(const char *prog)
{
  printf("Usage: %s [-DsbdlHOLC3]\n", prog);
  puts("  -D --device   device to use (default /dev/spidev1.1)\n"
       "  -s --speed    max speed (Hz)\n"
       "  -d --delay    delay (usec)\n"
       "  -b --bpw      bits per word \n"
       "  -l --loop     loopback\n"
       "  -H --cpha     clock phase\n"
       "  -O --cpol     clock polarity\n"
       "  -L --lsb      least significant bit first\n"
       "  -C --cls      clear screen\n"
       "  -3 --3wire    SI/SO signals shared\n");
  exit(1);
}

static const struct option lopts[] = {

  // SPI options. 
  { "device",  1, 0, 'D' },
  { "speed",   1, 0, 's' },
  { "delay",   1, 0, 'd' },

  { "reg",       1, 0, 'r' },
  { "val",       1, 0, 'v' },
  { "addr",      1, 0, 'a' },
  { "write8",    0, 0, 'w' },
  { "write16",   0, 0, 'W' },
  { "identify",  0, 0, 'i' },

  // Options for LCD
  { "text",      0, 0, 't' },
  { "cls",       0, 0, 'C' },


  { "i2c",       0, 0, 'I' },

  { NULL, 0, 0, 0 },
};



static int parse_opts(int argc, char *argv[])
{
  while (1) {
    int c;

    c = getopt_long(argc, argv, "D:s:d:r:v:a:wWitCI", lopts, NULL);

    if (c == -1)
      break;

    switch (c) {
    case 'D':
      device = optarg;
      break;
    case 's':
      speed = atoi(optarg);
      break;
    case 'd':
      delay = atoi(optarg);
      break;
    case 'r':
      reg = atoi(optarg);
      break;
    case 'v':
      val = atoi(optarg);
      break;
    case 'a':
      sscanf (optarg, "%x", &addr);
      break;

    case 'w':
      write8mode = 1;
      break;
    case 'W':
      write16mode = 1;
      break;
    case 'i':
      ident = 1;
      break;
    case 'I':
      mode=I2C_MODE;
      device = "/dev/i2c-0";
      break;
  
    case 't':
      text = 1;
      break;

    case 'C':
      cls = 1;
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

  printf("spi mode: %d\n", spi_mode);
  printf("bits per word: %d\n", bits);
  printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);
}


int main(int argc, char *argv[])
{
  int fd;
  int nonoptions;
  char buf[0x100];
  int i;

  nonoptions = parse_opts(argc, argv);

  if (write8mode && write16mode) {
    fprintf (stderr, "Can't use write8 and write16 at the same time\n");
    exit (1);
  }

  fd = open(device, O_RDWR);
  if (fd < 0)
    pabort("can't open device");

  if (mode == SPI_MODE) setup_spi_mode (fd);

  if (ident) 
    do_ident (fd);

  if (cls) set_reg_value8 (fd, 0x10, 0xaa);

  if (write8mode || write16mode) {
    for (i=nonoptions;i<argc;i++) {
      if (sscanf (argv[i], "%x:%x", &reg, &val) == 2) {

	if (write8mode) 
	  set_reg_value8 (fd, reg, val);
	else 
	  set_reg_value16 (fd, reg, val);

      } else {
	fprintf (stderr, "dont understand reg:val in: %s\n", argv[i]);
	exit (1);
      }
    }
  }

  if (reg != -1) 
    set_reg_value8 (fd, reg, val); 

  if (text) {
    buf [0] = 0;
    for (i=nonoptions; i < argc;i++) {
      if (i != nonoptions) strcat (buf, " ");
      strcat (buf, argv[i]);
    }
    send_text (fd, buf);
  }

  close(fd);

  exit (0);
}
