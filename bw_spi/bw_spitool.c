/*
 * bw_spitool.c. 
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
 *
 *
 * Compile on raspberry pi with 
 *
 * gcc -Wall -O2 bw_spitool.c -o bw_spitool
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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))


static const char *device = "/dev/spidev0.0";
static uint8_t mode;
static uint8_t bits = 8;
static uint32_t speed = 1000000;
static uint16_t delay = 15;
static int addr;

static int reg = -1;
static int val = -1;
static int write8mode, write16mode;

static void pabort(const char *s)
{
  perror(s);
  abort();
}


static void spi_txrx (int fd, int len, char *buf)
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

#if 0
static void transfer(int fd, int len, char *buf)
{
  int ret;
  uint8_t rx[0x80] = {0, };
  struct spi_ioc_transfer tr = {
    .tx_buf = 0,
    .rx_buf = (unsigned long)rx,
    .len = 0,
    .delay_usecs = delay,
    .speed_hz = speed,
    .bits_per_word = bits,
  };

  tr.tx_buf = (unsigned long)buf;	
  tr.len = len;
  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1)
    pabort("can't send spi message");

#if 0
  for (ret = 0; ret < len ; ret++) {
    if (!(ret % 6))
      puts("");
    printf("%.2X ", rx[ret]);
  }
  puts("");
#endif
}
#endif


#if 0
static void send_text (int fd, char *str) 
{
  char *buf; 
  int l;
  l = strlen (str);
  buf = malloc (l + 5); 
  buf[0] = addr;
  buf[1] = 0; 
  strcpy (buf+2, str); 
  spi_txrx (fd, l+2, buf);
  free (buf);
}
#endif

static void set_reg_value8 (int fd, int reg, int val)
{
  char buf[5]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  spi_txrx (fd, 3, buf);
}



static void set_reg_value16 (int fd, int reg, int val)
{
  char buf[5]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  buf[3] = val >> 8;
  spi_txrx (fd, 4, buf);
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
       "  -C --cs-high  chip select active high\n"
       "  -3 --3wire    SI/SO signals shared\n");
  exit(1);
}

static const struct option lopts[] = {

  // SPI options. 
  { "device",  1, 0, 'D' },
  { "speed",   1, 0, 's' },
  { "delay",   1, 0, 'd' },

  // text display options. 
  { "reg",       1, 0, 'r' },
  { "val",       1, 0, 'v' },
  { "addr",      1, 0, 'a' },
  { "write8",    0, 0, 'w' },
  { "write16",   0, 0, 'W' },

  { NULL, 0, 0, 0 },
};



static int parse_opts(int argc, char *argv[])
{
  while (1) {
    int c;

    c = getopt_long(argc, argv, "D:s:d:r:v:a:wW", lopts, NULL);

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

    default:
      print_usage(argv[0]);
      break;
    }
  }
  return optind; 
}


int main(int argc, char *argv[])
{
  int ret = 0;
  int fd;
  int nonoptions;
  //char buf[0x100];
  int i;

  nonoptions = parse_opts(argc, argv);

  if (write8mode && write16mode) {
    fprintf (stderr, "Can't use write8 and write16 at the same time\n");
    exit (1);
  }

  fd = open(device, O_RDWR);
  if (fd < 0)
    pabort("can't open device");




  /*
   * spi mode
   */
  ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
  if (ret == -1)
    pabort("can't set spi mode");

  ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
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

  // printf("spi mode: %d\n", mode);
  // printf("bits per word: %d\n", bits);
  // printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);

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

  close(fd);

  exit (0);
}
