/*
 * bw_tool.c. 
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
#include <sys/errno.h>
#include <termios.h>
#include <time.h>

#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <linux/i2c-dev.h>


#include "usb_protocol.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))


enum {MODE_NONE = -1, SPI_MODE = 1, I2C_MODE, USB_I2CMODE, USB_SPIMODE }; 
static int mode = MODE_NONE;

static const char *device = NULL; // = "/dev/spidev0.0";
static uint8_t spi_mode;
static uint8_t bits = 8;

static uint32_t speed = 100000;
#define MAX_READ_SPEED 100000 

static uint16_t delay = 20;
static int addr = 0x82;
static int text = 0;
static char *monitor_file;
static int readmode = 0;
static int xtendedvalidation = 0, valid=1;

static int rs485_lid = 0, rs485_rid = -1;

static int reg = -1;
static long long val = -1;
static int cls = 0;
static int write8mode, writemiscmode, ident, readee;
static int scan = 0;
static int hexmode = 0;
static char numberformat = 'x';
static int mode2 = 0;
static char tidfnamebuf[0x100], *tidfname;

static int tid; // Transaction ID. Best if not recycled... (but wraps after 256). 

static int debug = 0;
#define DEBUG_REGSETTING 0x0001
#define DEBUG_TRANSFER   0x0002

static void pabort(const char *s)
{
  if (errno)
    perror(s);
  else
    fprintf (stderr, "%s.\n", s);
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
  printf ("%s %d:", t, n);
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

  // if (rlen > tlen) tr.len = rlen; 
  // else             tr.len = tlen;
  tr.len = tlen + rlen;
  tr.tx_buf = (unsigned long) buf; 
  tr.rx_buf = (unsigned long) buf; 
  
  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1)
    pabort("can't send spi message");
  valid = buf[1] != 0;

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
    if (!cr) break;
    if (cr < 0) return nr?nr:cr;
    nr += cr;
  }
  return nr;
}


int myread_to (int fd, unsigned char *buf, int len, int to)
{
  fd_set read_fds, write_fds, except_fds;
  int nr, cr, rv;
  struct timeval timeout;

  timeout.tv_sec  = to / 1000000;
  timeout.tv_usec = to % 1000000;

  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);
  FD_ZERO(&except_fds);
  FD_SET(fd, &read_fds);

  nr = 0;
  while (nr < len) {
    // XXX we count on timeout being modified. Linux is documented to
    // do this, others usually don't.
    rv = select(fd + 1, &read_fds, &write_fds, &except_fds, &timeout);
    if (rv > 0) {
      if (FD_ISSET (fd, &read_fds)) {
	cr = read (fd,  buf+nr, len-nr);
	if (!cr) break; // handle EOF. 
	if (cr < 0) return nr?nr:cr;
	nr += cr;
      }
    } else if (rv == 0) { 
      // timeout. 
      return nr;
    } else {
      // error. 
      return nr?nr:-1;
    }
  }
  return nr;
}



static void usb_spitxrx (int fd, unsigned char *buf, int tlen, int rlen)
{
  int hdrlen;
  //   static int slave = -1;
  static char cmd[] = { BINSTART, 
			USB_CMD_FWD, 0 /*Local ID */, 0 /* Len */,
			USB_CMD_SPI_TXRX, 0 /* ID */, 0 /* Len */  };
  
  if (rs485_rid == -1) {
    // Local
    cmd[1] = USB_CMD_SPI_TXRX;
    cmd[2] = rs485_lid;
    cmd[3] = tlen + rlen; 
    hdrlen = 4;
  } else {
    cmd[1] = USB_CMD_FWD;
    cmd[2] = rs485_lid;
    cmd[3] = tlen + rlen + 3; 
    cmd[4] = USB_CMD_SPI_TXRX;
    cmd[5] = rs485_rid;
    cmd[6] = tlen + rlen; 
    hdrlen = 7;
  }
  
  if (write (fd, cmd, hdrlen) != hdrlen) {
    pabort ("can't write USB cmd");
  }
  if (write (fd, buf, tlen+rlen) != tlen+rlen) {
    pabort ("can't write USB buf");
  }

  //XXX: check return code. 
  if (myread_to (fd, buf, 4, 1000000) != 4) {
    pabort ("can't read USB");
  }

  if (buf[0] != BINSTART)
    pabort ("invalid binstart code from USB");

  if (buf[1] != (USB_CMD_SPI_TXRX | USB_RESPONSE))
    pabort ("invalid response code from USB");

  if (buf[3] != tlen+rlen) {
    printf ("got %d instead of %d: ", buf[3], tlen+rlen);
    pabort ("invalid length code from USB");
  }
  if (myread_to (fd, buf, tlen+rlen, 1000000) != tlen+rlen) 
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


#define CRC16 0x8005


uint16_t crc16 (uint16_t crc, unsigned char *data, int size)
{
  int i;
  unsigned char c;

  /* Sanity check: */
  if(data == NULL)
    return 0;

  while(size > 0) {
    c = *data++;
    size--;
    for (i=0;i<8;i++) {
      /* the shift part*/
      if (crc & 0x8000) 
        crc = (crc << 1) ^ CRC16;       /* the feedback part: */
      else
        crc = (crc << 1);

      crc ^= c & 1; // get next bit. 
      c >>= 1;
    }
  }
  return crc;
}


static void transfer(int fd, unsigned char *buf, int tlen, int rlen)
{
  if (debug & DEBUG_TRANSFER) 
    dump_buf ("tx", buf, tlen);
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
    dump_buf ("rx", buf, tlen+rlen);
}


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




unsigned long long get_value (unsigned char *buf, int len)
{
  if (len == 1)
    return *buf;
  if (len == 2)
    return get_value (buf, 1) | get_value (buf+1, 1) << 8;
  if (len == 4)
    return get_value (buf, 2) | get_value (buf+2, 2) << 16;
  if (len == 8)
    return get_value (buf, 4) | get_value (buf+4, 4) << 32;
  return *buf;
}


#define FLAG_ADDR 1
#define FLAG_ERR  2
#define FLAG_DBG  4

static void do_ident (int fd, int a, int flags)
{
  unsigned char buf[0x20];
  int i;
  int bp, crc; 


#define IDLEN 0x18
  if (mode2) {
    //flags |= FLAG_DBG;
    //usleep (200);
    bp = 0;
    buf [bp++] = a;
    buf [bp++] = 0xc1;
    buf [bp++] = tid;
    buf [bp++] = IDLEN;
    buf [bp++] = 1;
    crc = crc16 (0, buf, bp);
    buf [bp++] = crc;
    buf [bp++] = crc >> 8;
    //if (flags & FLAG_DBG) if (debug & DEBUG_TRANSFER) dump_buf ("D: ident before TX: ", buf, bp);
    
    transfer (fd, buf, bp,0);
    usleep (700); 
    buf[0] = a | 1;
    transfer (fd, buf, IDLEN+7,0);
    //if (flags & FLAG_DBG) if (debug & DEBUG_TRANSFER) dump_buf ("nD: ident got:", buf, IDLEN+7);

    //printf ("D:<%02x>\n", a);fflush (stdout);
    
    if (buf [1] != a) {
      //printf ("xx");fflush (stdout);
      if (flags & FLAG_ERR) 
	printf ("E: ident: Didn't get addr back: %02x\n", buf[1]);
      return;
    }
    //printf ("yy");fflush (stdout);
    if (buf [2] != 0xaa) {
      //printf ("zz");fflush (stdout);
      if (flags & FLAG_ERR) 
	printf ("E: ident: Didn't get read ack: %02x\n", buf[2]);
      return;
    }
    //printf ("aa");fflush (stdout);

    if (buf [3] != (tid & 0xff)) {
      //printf ("bb");fflush (stdout);
      if (flags & FLAG_ERR) 
	printf ("E: ident: Didn't get tid: %02x/%02x\n", buf[2], tid &0xff);
      return;
    }
    //printf ("cc");fflush (stdout);

    crc = crc16 (0, buf+1, IDLEN+3);
    if (get_value (buf+IDLEN+4, 2) != crc) {
      //printf ("dd");fflush (stdout);
      if (flags & FLAG_ERR) 
	printf ("E: ident: invalid CRC: %04llx/%04x\n", 
		get_value (buf+IDLEN+3, 2), crc);
      return;
    }
    i = 4;
    //printf ("ee");fflush (stdout);
    //printf ("D: ident got: ");
    //dump_buffer (buf, IDLEN+7);
    //printf ("\n");
    
    if (flags & FLAG_ADDR) 
      printf ("%02x: ", a);

  } else {
    buf [0] = addr | 1;
    buf [1] = 1;
    
    transfer (fd, buf, 0x2,0x20);
    i = 2;
  }

  for ( ;i<0x20;i++) {
    if (!buf[i]) break;
    printf ("%c", buf[i]);
    //putchar (buf[i]);
  }
  printf ("\n");
  //putchar ('\n');
  fflush (stdout);
}



char mkprintable (char ch)
{
  if (ch < ' ') return '.';
  if (ch <= '~') return ch;
  return '.';
}


static void do_scan (int fd)
{
  unsigned char buf[0x20];
  int add;
  int i;

  if (mode2) {
    //printf ("Scanning.\n");
    for (add = 0;add < 255;add += 2) {
      //printf ("scan %d starting.\n", add);
      do_ident (fd, add, FLAG_ADDR);
      //printf ("scan %d done. \n", add);
    }
    return;
  }


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



static void print_usage(const char *prog)
{
  printf("Usage: %s [-DsbdlHOLC3]\n", prog);
  puts("  -D --device   device to use (default /dev/spidev1.1)\n"
       "  -s --speed    max speed (Hz)\n"
       "  -d --delay    delay (usec)\n"
       "  -r --reg      \n"
       "  -v --val      value\n"
       "  -a --addr     address\n"
       "  -w --write8   write an octet\n"
       "  -W --write    write arbitrary type\n"
       "  -i --identify Identify the indicated device\n"
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
  { "xtend",   0, 0, 'x' },

  { "mode2",     0, 0, '2' },
  { "tidfile",   1, 0, 'T' },

  { "reg",       1, 0, 'r' },
  { "val",       1, 0, 'v' },
  { "addr",      1, 0, 'a' },
  { "write8",    0, 0, 'w' },
  { "write",     0, 0, 'W' },
  { "identify",  0, 0, 'i' },
  { "scan",      0, 0, 'S' },
  { "read",      0, 0, 'R' },
  { "eeprom",    0, 0, 'e' },



  // Options for LCD
  { "text",      0, 0, 't' },
  { "cls",       0, 0, 'C' },
  { "monitor",   1, 0, 'm' },

  { "hex",       0, 0, 'h' },

  { "i2c",       0, 0, 'I' },
  { "usbspi",    0, 0, 'u' },
  { "usbi2c",    0, 0, 'U' },
  { "decimal",   0, 0, '1' },

  { "rs485_ids", 1, 0, '4' },

  { "verbose",   1, 0, 'V' },
  { "help",      0, 0, '?' },
  { NULL, 0, 0, 0 },
};



static int parse_opts(int argc, char *argv[])
{
  int r;

  while (1) {
    int c;

    c = getopt_long(argc, argv, "D:s:d:r:v:a:wWietxCm:I1SRUu4:V:2", lopts, NULL);

    if (c == -1)
      break;

    switch (c) {
    case 'D':
      device = strdup (optarg);
      if (mode == MODE_NONE) {
	if (strstr (device, "i2c")) mode=I2C_MODE;
	else                        mode=SPI_MODE;
      }
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
    case 'V':
      debug = atoi(optarg);
      break;
    case 'a':
      sscanf (optarg, "%x", &addr);
      break;

    case 'e':
      readee = 1;
      break;
    case 'h':
      hexmode = 1;
      break;
    case 'w':
      write8mode = 1;
      break;
    case 'W':
      writemiscmode = 1;
      break;
    case 'R':
      if (speed > MAX_READ_SPEED) speed = MAX_READ_SPEED;
      readmode = 1;
      break;
    case 'i':
      if (speed > MAX_READ_SPEED) speed = MAX_READ_SPEED;
      ident = 1;
      break;
    case 'I':
      mode=I2C_MODE;
      if (!device) 
	device = "/dev/i2c-0";
      break;
    case 'S':
      if (speed > MAX_READ_SPEED) speed = MAX_READ_SPEED;
      scan=1;
      break;  
    case 't':
      text = 1;
      break;

    case 'x':
      xtendedvalidation = 1;
      break;

    case 'C':
      cls = 1;
      break;

    case '1':
      numberformat = 'd';
      break;

    case 'm':
      monitor_file = strdup (optarg);
      break;

    case 'u':
      mode = USB_SPIMODE;
      if (!device) 
	device = "/dev/ttyACM0";
      break;
    case 'T':
      tidfname = strdup (optarg);
      break;
    case '2':
      mode2 = 1;
      break;
    case '4':
      r = sscanf (optarg, "%d:%d", &rs485_lid, &rs485_rid);
      if (r == 0) {
	fprintf (stderr, "no RS485ID found\n");
	exit (1);
      }
      break;
    case 'U':
      mode = USB_I2CMODE;
      if (!device)
	device = "/dev/ttyACM0";
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

  if (!device) 
    device = "/dev/spidev0.0";

  if (mode == MODE_NONE) 
    mode = SPI_MODE; 

  return optind; 
}


void setup_spi_mode (int fd)
{
  int ret;
  /*
   * spi mode
   */
  //printf ("setting spi mode. %d/%d\n", speed, spi_mode);
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

void setup_virtual_serial (int fd)
{
  struct termios tio;

  memset (&tio, 0, sizeof (tio));
  if (tcgetattr (fd, &tio) < 0)
    pabort ("TCGETS on device");

  tio.c_lflag = 0;   
  tio.c_oflag = 0;      
  tio.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

  tio.c_cflag |= (CLOCAL | CREAD);
  tio.c_cflag &= ~(PARENB | PARODD);    
  if (tcsetattr (fd, TCSANOW, &tio) != 0) 
    pabort ("TCSETS on devcie");
}

void init_device (int fd)
{
  //printf ("init device %d.\n", speed);
  switch (mode) {
  case SPI_MODE:
    setup_spi_mode (fd);
    break;
  case USB_SPIMODE:
  case USB_I2CMODE:
    setup_virtual_serial (fd);
    break;
  case I2C_MODE:
    // nothing special. 
    break;
  }
}

int typelen (char typech)
{
  
  switch (typech) {
  case 'b':return 1;
  case 's':return 2;
  case 'i':return 4;
  case 'l':return 8;
  default:
    fprintf (stderr, "Don't understand the type '%c'\n", typech);
    exit (1);
  }
}

char *formatstr (char typech)
{
  if (numberformat != 'x') return "%lld";
  switch (typech) {
  case 'b':return "%02llx ";
  case 's':return "%04llx ";
  case 'i':return "%08llx ";
  case 'l':return "%016llx ";
  }
  return "%lld"; // should not happen. 
}


int get_update_tid (void)
{
  //char *tidp;
  //char buf[32];
  int ltid;
  FILE *fp;

  fp = fopen (tidfname, "r");
  if (!fp || (fscanf (fp, "%d", &ltid) < 1)) {
    srand (time (NULL));
    ltid = rand () & 0xff;
  }
  if (fp) fclose (fp);
  
  fp = fopen (tidfname, "w");
#if 1
  if (!fp) {
    fprintf (stderr, "Can't open %s", tidfname);
    perror ("");
    exit (1);
  }
#endif

  fprintf (fp, "%d\n", ltid+1);
  fclose (fp);

  return ltid;
}



int main(int argc, char *argv[])
{
  int fd;
  int nonoptions;
  unsigned char buf[0x100];
  int i, rv;
  char typech;
  char format[32];
  unsigned char tbuf[0x100];
  int bp, crc, rlen;
  int tries;
#define MAXTRIES 5

  if (argc <= 1) {
    print_usage (argv[0]);
    exit (0);
  }
  sprintf (tidfnamebuf, "%s/.tid", getenv ("HOME"));
  tidfname = tidfnamebuf; 

  nonoptions = parse_opts(argc, argv);

  if (write8mode && writemiscmode) {
    fprintf (stderr, "Can't use write8 and write misc at the same time\n");
    exit (1);
  }

  //fprintf (stderr, "dev = %s\n", device);
  //fprintf (stderr, "mode = %d\n", mode);
  fd = open(device, O_RDWR);
  if (fd < 0)
    pabort("can't open device");

  init_device (fd);

  if (ident)
    do_ident (fd, addr, FLAG_ERR | FLAG_DBG);

  if (readee) 
    do_readee (fd);

  if (cls) set_reg_value8 (fd, 0x10, 0xaa);

  if (write8mode) {
    sprintf (format, "%%x:%%ll%c", numberformat);
    for (i=nonoptions;i<argc;i++) {
      if (sscanf (argv[i], format, &reg, &val) == 2) {
	if (debug & DEBUG_REGSETTING)
           fprintf (stdout, "Writing register 0x%02X val 0x%08llX\n",reg,val);
        set_reg_value8 (fd, reg, val);
      } else {
        fprintf (stderr, "dont understand reg:val in: %s\n", argv[i]);
        exit (1);
      }
    }
    exit (0);
  }

  tid = get_update_tid ();
  //printf ("Got tid=%d(0x%02x).\n", tid, tid);
  if (writemiscmode) {
    sprintf (format, "%%x:%%ll%c:%%c", numberformat);
    if (mode2) {
      bp=0;
      tbuf[bp++] = addr;
      tbuf[bp++] = 0xc2;
      tbuf[bp++] = tid;
    }
      
    for (i=nonoptions;i<argc;i++) {
      typech = 'b';
      rv = sscanf (argv[i], format, &reg, &val, &typech);
      if (rv < 2) {
        fprintf (stderr, "don't understand reg:val:type in: %s\n", argv[i]);
        exit (1);
      }

      if (mode2) {
	switch (typech) {
	case 'b':tbuf[bp++] = 1;tbuf[bp++] = reg;tbuf[bp++] = val;break;
	case 's':tbuf[bp++] = 2;tbuf[bp++] = reg;
	  tbuf[bp++] = (val >> 0) & 0xff;
	  tbuf[bp++] = (val >> 8) & 0xff;
	  break;
	case 'i':tbuf[bp++] = 4;tbuf[bp++] = reg;
	  tbuf[bp++] = (val >>  0) & 0xff;
	  tbuf[bp++] = (val >>  8) & 0xff;
	  tbuf[bp++] = (val >> 16) & 0xff;
	  tbuf[bp++] = (val >> 24) & 0xff;
	  break;
	case 'l':tbuf[bp++] = 8;tbuf[bp++] = reg;
	  tbuf[bp++] = (val >>  0) & 0xff;
	  tbuf[bp++] = (val >>  8) & 0xff;
	  tbuf[bp++] = (val >> 16) & 0xff;
	  tbuf[bp++] = (val >> 24) & 0xff;
	  tbuf[bp++] = (val >> 32) & 0xff;
	  tbuf[bp++] = (val >> 40) & 0xff;
	  tbuf[bp++] = (val >> 48) & 0xff;
	  tbuf[bp++] = (val >> 56) & 0xff;
	default:
	  fprintf (stderr, "Don't understand the type value in %s\n", argv[i]);
	  exit (1);
	}
      } else {
	switch (typech) {
	case 'b':	set_reg_value8  (fd, reg, val);break;
	case 's':	set_reg_value16 (fd, reg, val);break;
	case 'i':	set_reg_value32 (fd, reg, val);break;
	case 'l':	set_reg_value64 (fd, reg, val);break;
	default:
	  fprintf (stderr, "Don't understand the type value in %s\n", argv[i]);
	  exit (1);
	}
      }
    }
    if (mode2) {
      crc = crc16 (0, tbuf, bp);
      tbuf[bp++] = crc & 0xff;
      tbuf[bp++] = crc >> 8;
      //if (debug & DEBUG_TRANSFER) dump_buf ("D: m2: Before tx:", tbuf, bp);

      if (bp > 33) printf ("W: Transfer %d > 32 bytes. Target may not support this.\n", bp);
      transfer (fd, tbuf, bp, 0);
      usleep (100);
      tbuf[0] = addr + 1;
      transfer (fd, tbuf, 8, 0);
      //if (debug & DEBUG_TRANSFER) dump_buf ("D:       got: ", tbuf, 8);
      if (tbuf[1] != addr) 
	printf ("E: Didn't return addr: %02x\n", tbuf[1]);
      if (tbuf[3] != (tid & 0xff))
	printf ("E: Didn't get tid: %02x/%02x\n", tbuf[3], tid & 0xff);
      if (tbuf[2] == 0xcc) {
	crc = crc16 (0, tbuf+1, 3);
	if (crc != get_value (tbuf+4, 2)) 
	  printf ("E: Invalid checksum on write-ack: %04x/%04llx\n", crc, get_value(tbuf+4,2));
	// All ok. do nothing. 

      } else  if (tbuf[2] == 0xee) {
	// XXX check checksum. 
	printf ("E: Got badCRC reply! slave expected: %02x%02x\n", tbuf[4], tbuf[3]);
      } else {
	printf ("E: got unexpected reply type: %02x\n", tbuf[2]);
      }
    }    exit (0);
  }

  if (readmode) {
    if (mode2) {
      bp = 0;
      rlen = 0;
      tbuf[bp++] = addr;
      tbuf[bp++] = 0xc1;
      tbuf[bp++] = tid;
      for (i=nonoptions;i<argc;i++) {
	rv = sscanf (argv[i], "%x:%c", &reg, &typech);
	if (rv < 1) {
	  fprintf (stderr, "don't understand reg:type in: %s\n", argv[i]);
	  exit (1);
	}
	if (rv == 1) typech = 'b';
	rlen += tbuf[bp++] = typelen (typech);
	tbuf[bp++] = reg;
      }
      crc = crc16 (0, tbuf, bp);
      tbuf[bp++] = crc & 0xff;
      tbuf[bp++] = crc >> 8;

      //if (debug & DEBUG_TRANSFER) dump_buf ("D: m2read: sending: ", tbuf, bp);
      transfer (fd, tbuf, bp, 0);

      tries = 0;
      do {
	usleep (100);
	tbuf[0] = addr+1;
	transfer (fd, tbuf, rlen+6,0);
      } while ((tries++ < MAXTRIES) && (tbuf[2] == 0xbb));

      if (tries != 1) printf ("W: required %d tries.\n", tries);

      if ((rlen+6) > 33) printf ("W: Transfer %d > 32 bytes. Target may not support this.\n", rlen+6);

      //printf ("rlen=%2d  ", rlen);
      // if (debug & DEBUG_TRANSFER) dump_buf ("D: got: ", tbuf, rlen+6);

      if (tbuf[1] != addr) 
	printf ("E: Didn't return addr: %02x\n", tbuf[1]);
      if (tbuf[2] != 0xaa)
	printf ("E: Didn't get ack response: %02x\n", tbuf[2]);
      if (tbuf[3] != (tid & 0xff))
	printf ("E: Didn't get tid: %02x/%02x\n", tbuf[3], tid & 0xff);

      crc = crc16 (0, tbuf+1, rlen+3); // transferred rlen+6:  address+data+crc
      if ( get_value(tbuf+rlen+4, 2) != crc) {
	printf ("E: bad crc: %04llx / %04x \n",  get_value(tbuf+rlen+3, 2), crc);
      }
      bp=4;
      for (i=nonoptions;i<argc;i++) {
	typech = 'b'; // default.
	rv = sscanf (argv[i], "%*x:%c", &typech);
	printf (formatstr (typech), get_value (tbuf+bp, typelen (typech)));
	bp += typelen (typech);
      }
     
    } else {
      for (i=nonoptions;i<argc;i++) {
	typech = 'b'; // default.
	rv = sscanf (argv[i], "%x:%c", &reg, &typech);
	if (debug & DEBUG_REGSETTING)
	  fprintf (stdout, "Reading register 0x%02X type %c\n",reg,typech);
	if (rv < 1) {
	  fprintf (stderr, "don't understand reg:type in: %s\n", argv[i]);
	  exit (1);
	}

	switch (typech) {
	case 'b':	val = get_reg_value8  (fd, reg);break;
	case 's':	val = get_reg_value16 (fd, reg);break;
	case 'i':	val = get_reg_value32 (fd, reg);break;
	case 'l':	val = get_reg_value64 (fd, reg);break;
	default:
	  fprintf (stderr, "Don't understand the type value in %s\n", argv[i]);
	  exit (1);
	}
	
	if (xtendedvalidation && !valid) printf ("?");
	printf (formatstr(typech), val);
      }
    }
    printf ("\n");
    exit (0);
  }


  if (hexmode) {
    unsigned char buf[0x400];
    int l, v;

    l = argc - nonoptions; 
    for (i=nonoptions;i<argc;i++) {
      rv = sscanf (argv[i], "%x", &v);
      if (rv < 1) {
        fprintf (stderr, "don't understand reg:type in: %s\n", argv[i]);
        exit (1);
      }

      buf[i-nonoptions] = v;
    }
    dump_buf ("send: ", buf, l);
    transfer (fd, buf, l, 0);
    dump_buf ("got:  ", buf, l);
    printf ("\n");
    exit (0);
  }

  if (text) {
    buf [0] = 0;
    for (i=nonoptions; i < argc;i++) {
      if (i != nonoptions) strcat ((char*)buf, " ");
      strcat ((char*)buf, argv[i]);
    }
    send_text (fd, buf);
    exit (0);
  }

  if (reg != -1) 
    set_reg_value8 (fd, reg, val); 

  if (scan)
    do_scan (fd);

  if (monitor_file) 
    do_monitor_file (fd, monitor_file);

  close(fd);

  exit (0);
}
