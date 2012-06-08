//
//  How to access GPIO registers from C-code on the Raspberry-Pi
//  Example program
//  15-January-2012
//  Dom and Gert
//
// Modified to drive the SPI interface by
// R.E.Wolff@BitWizard.nl
// 18-04-2012. 
//

// The Makefile installs this (and not the gpio program) as setuid. 
// You can "break" the normal operation of the raspberry pi by 
// modfiying GPIO pins to do things the OS doesn't expect. For example,
// you can disable the uart (and thereby the login process on ttyAM0)
// by programming the GPIO pins for the UART to be input. 
//
// But provided nothing "serious" is connected to the hardware I2C port
// normal users cannot break anything. So for now access the I2C bus 
// without sudo is allowed. 
//


// Access from ARM Running Linux

#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */
#define BSC_BASE                 (GPIO_BASE  + 0x5000) /* BSC (I2C) controller */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)


// I/O access
volatile unsigned *gpio, *bsc;

/*****************************************************************/
/*             GPIO convenience functions                        */
/*****************************************************************/

int get_3b (int g)
{
   return (gpio[g/10] >> ((g%10)*3)) & 7;
}

void set_3b (int g, int v)
{
   gpio[g/10] = (gpio[g/10] & ~(7 << ((g%10)*3)))  |
                        ((v & 7) << ((g%10) *3));
}

void gpio_set (int g)
{
   gpio[7 + (g/32)] = 1 << (g %32);
}

void gpio_clr (int g)
{
   gpio[10 + (g/32)] = 1 << (g %32);
}

int gpio_get (int g)
{
   return (gpio[0xd + (g/32)] >> (g % 32)) & 1;
}


enum gpio_funcs {GP_INP,  GP_OUT,  GP_ALT5, GP_ALT4, 
                 GP_ALT0, GP_ALT1, GP_ALT2, GP_ALT3};

enum bsc_regs {BSC_C, BSC_S, BSC_DLEN, BSC_A, BSC_FIFO, 
               BSC_DIV, BSC_DEL, BSC_CLKT};
/*****************************************************************/
/*             SPI  convenience functions                        */
/*****************************************************************/



void setup_io();

int main(int argc, char **argv)
{ 
  int i, n;
  unsigned int v;
  int t;

  // Set up gpi pointer for direct register access
  setup_io();

  // Switch the GPOIs of the BSC module to their BSC(I2C) mode. 
  set_3b (0, GP_ALT0);
  set_3b (1, GP_ALT0);

  bsc[BSC_DIV]  = 625; // 400kHz. (core clock is 250Mhz).
  if (strstr (argv[0], "read")) {
    sscanf (argv[1], "%x", &n);
    bsc[BSC_DLEN]  = n; //  num bytes to recieve. 
    bsc[BSC_C] = 0x8081; // Enable controller and start the transfer. 
    t = 0;
    for (i=0;i<n;i++) {
      while (! (bsc[BSC_S] & 0x20)) {
        t++;
      }
      printf ("%02x ", bsc[BSC_FIFO]);
    }
    printf ("\n");
    //printf ("t=%d\n", t);
  } else {
    bsc[BSC_DLEN]  = argc-2; //  num bytes to send. 

    sscanf (argv[1], "%x", &v);
    bsc[BSC_A]  = v >> 1;  // slave address.
  
    bsc[BSC_C] = 0x8080; // Enable controller and start the transfer. 

    // XXX This assumes all bytes fit in the FIFO. 
    for (i=2;i<argc;i++) {
      sscanf (argv[i], "%x", &v);
      bsc[BSC_FIFO] = v;
    }
    // XXX need timeout. 
    // XXX Busy waiting is ugly. 
    t = 0;
    while (! (bsc[BSC_S] & 0x2)) 
      if (t++ > 100000) break; // Wait for TX FIFO to drain. 
    //printf ("t=%d\n", t);
  }

  bsc[BSC_S] = 0x2; // CLear the Done flag by writing a 1 to it. 

  bsc[BSC_C] = 0x8000; // Enable controller and stop the transfer. 

  return 0;
} // main


//
// Set up a memory region to access hardware. 
//
volatile unsigned *get_mmap_ptr (unsigned pos, unsigned len)
{
   int mem_fd;
   void *ptr;

   /* open /dev/mem */
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem \n");
      exit (-1);
   }

   /* mmap GPIO */

   // Allocate MAP block
   if ((ptr = malloc(len + (PAGE_SIZE-1))) == NULL) {
      printf("allocation error \n");
      exit (-1);
   }

   // Make sure pointer is on 4K boundary
   if ((unsigned long)ptr % PAGE_SIZE)
     ptr += PAGE_SIZE - ((unsigned long)ptr % PAGE_SIZE);

   // Now map it
   ptr = (unsigned char *)mmap(
      (caddr_t)ptr,
      len,
      PROT_READ|PROT_WRITE,
      MAP_SHARED|MAP_FIXED,
      mem_fd,
      pos
   );

   if ((long)ptr < 0) {
      printf("mmap error %ld\n", (long)ptr);
      exit (-1);
   }

   close (mem_fd);
   // Always use volatile pointer!
   return (volatile unsigned *)ptr;
}

//
// set up the pointers to GPIO and SPI controllers. 
// 
void setup_io()
{
   gpio = get_mmap_ptr (GPIO_BASE, BLOCK_SIZE);
   bsc = get_mmap_ptr (BSC_BASE, BLOCK_SIZE);
} // setup_io



