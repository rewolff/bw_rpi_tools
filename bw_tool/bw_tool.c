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
#include <sys/stat.h>
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
static char *monitor_file;

static int reg = -1;
static int val = -1;
static int cls = 0;
static int write8mode, write16mode, ident;

static void pabort(const char *s)
{
  perror(s);
  abort();
}


static void spi_txrx (int fd, char *buf, int tlen, int rlen)
{
  int ret;
  struct spi_ioc_transfer tr = {
    .delay_usecs = delay,
    .speed_hz = speed,
    .bits_per_word = bits,
  };

  if (rlen > tlen) tr.len = rlen; 
  else             tr.len = tlen; 
  tr.tx_buf = (unsigned long) buf; 
  tr.rx_buf = (unsigned long) buf; 
  
  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1)
    pabort("can't send spi message");

}


static void i2c_txrx (int fd, char *buf, int tlen, int rlen)
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
     if (read (fd, buf, rlen) != rlen) 
        pabort ("can't read i2c");
}


static void transfer(int fd, char *buf, int tlen, int rlen)
{
  if (mode == SPI_MODE) 
     spi_txrx (fd, buf, tlen, rlen);
  else
     i2c_txrx (fd, buf, tlen, rlen);
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
  transfer (fd, buf, l+2, 0);
  free (buf);
}


static void set_reg_value8 (int fd, int reg, int val)
{
  char buf[5]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  transfer (fd, buf, 3, 0);
}


static void set_reg_value16 (int fd, int reg, int val)
{
  char buf[5]; 

  buf[0] = addr;
  buf[1] = reg;
  buf[2] = val;
  buf[3] = val >> 8;
  transfer (fd, buf, 4, 0);
}


static void do_ident (int fd)
{
  char buf[0x20];
  int i;

  buf [0] = addr | 1;
  buf [1] = 1;

  transfer (fd, buf, 0x2,0x20);

  for (i= (mode==I2C_MODE)?0:2 ;i<0x20;i++) {
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
  { "monitor",   1, 0, 'm' },


  { "i2c",       0, 0, 'I' },

  { NULL, 0, 0, 0 },
};



static int parse_opts(int argc, char *argv[])
{
  while (1) {
    int c;

    c = getopt_long(argc, argv, "D:s:d:r:v:a:wWitCm:I", lopts, NULL);

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

    case 'm':
      monitor_file = strdup (optarg);
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



void wait_for_file_changed (char *fname)
{
  static time_t lastmtime = 0;
  struct stat statb;

  while (1) {
    if (stat (fname, &statb) < 0) {
      pabort (fname);
    }

    if (lastmtime != statb.st_mtime) {
      printf ("fc: %x / %x\n", (int) lastmtime, (int) statb.st_mtime);
      lastmtime = statb.st_mtime;
      return;
    }
    usleep (250000);
  }
}


char *get_file_line (char *fname, int lno)
{
  static char buf[0x50], *p;
  FILE *f;
  int i;


  f = fopen (fname, "r");
  if (!f) pabort (fname);
  for (i = 0;i<=lno;i++) {
    buf [0] = 0;
    p = fgets (buf, 0x3f, f);
    if (!p) return p;
  }
  fclose (f);
  buf[strlen(buf)-1] = 0; // chop!
  return buf;
}


void do_monitor_file (int fd, char *fname)
{
  int i;
  char *buf;
  char olddisplay[4][0x20];

  fprintf (stderr, "monitoring %s on fd %d.\n", fname, fd);
  while (1) {
    wait_for_file_changed (fname);
    //set_reg_value8 (fd, 0x10, 0xaa);
    for (i=0;i<4;i++) {
      buf = get_file_line (fname, i);
      if (!buf) break;
      buf[20] = 0;
      if (strcmp (buf, olddisplay[i])) { 
        strcpy (olddisplay[i], buf);
        set_reg_value8 (fd, 0x11, i<<5);
        send_text (fd, buf);
        usleep (50000);
      }
    }
  }
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

  //fprintf (stderr, "dev = %s\n", device);
  //fprintf (stderr, "mode = %d\n", mode);
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

  if (monitor_file) 
    do_monitor_file (fd, monitor_file);

  close(fd);

  exit (0);
}
