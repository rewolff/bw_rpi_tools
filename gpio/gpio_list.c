//
//  How to access GPIO registers from C-code on the Raspberry-Pi
//  Example program
//  15-January-2012
//  Dom and Gert
//
// Modified to provide a useable commandline userinterface 
// R.E.Wolff@BitWizard.nl 
// 18-04-2012. 
//

// Access from ARM Running Linux


// This is for the original "pi"
#define BCM2708_PERI_BASE        0x20000000
// This is for the new (as of late 2014/early 2015) "pi 2" (with BCM2836)
//#define BCM2708_PERI_BASE 0x3F000000     

#define GPIO_BASE   (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */


#define BCM2835_NUMIOS 55


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SIZE (4*1024)
#define BLOCK_SIZE (4*1024)


// I/O access
volatile unsigned *gpio;


#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0


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


volatile unsigned * setup_io();

int main(int argc, char **argv)
{ 
  int i;
  int g;

  char *gp_funcs[] = {"INP",  "OUT",  "ALT5", "ALT4", 
                     "ALT0", "ALT1", "ALT2", "ALT3"};

  // Set up gpi pointer for direct register access
  gpio = setup_io();

  if (strstr (argv[0], "list")) {
    for (i=0;i<BCM2835_NUMIOS;i++) {
      if ((i%10) == 0) printf ("%4d    ", i);
      printf ("%4s ", gp_funcs [get_3b (i)]);
      if ((i%10) == 9) printf ("\n");
    }
    printf ("\n");
  } else { 
    g = atoi (argv[1]);
    if (g > BCM2835_NUMIOS) {
      fprintf (stderr, "IO pin number out of range.\n");
      exit (1);
    }
    if (strstr (argv[0], "setfunc")) {
      for (i=0;i<8;i++) if (strcmp (argv[2], gp_funcs[i]) == 0) break;
      if (i == 8) {
         printf ("Unknown function: %s\n", argv[2]);
         exit (1); 
      }
      set_3b (g, i);
    } else if (strstr (argv[0], "set")) {
      gpio_set (g);
    } else if (strstr (argv[0], "clr")) {
      gpio_clr (g);
    } else if (strstr (argv[0], "get")) {
      printf ("%d\n", gpio_get (g));
    }
  }
  return 0;

} // main


//
// Set up a memory region to access GPIO
//
volatile unsigned int *setup_io()
{
   int  mem_fd;
   char *gpio_mem, *gpio_map;


   /* open /dev/mem */
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem \n");
      exit (-1);
   }

   /* mmap GPIO */

   // Allocate MAP block
   if ((gpio_mem = malloc(BLOCK_SIZE + (PAGE_SIZE-1))) == NULL) {
      printf("allocation error \n");
      exit (-1);
   }

   // Make sure pointer is on 4K boundary
   if ((unsigned long)gpio_mem % PAGE_SIZE)
     gpio_mem += PAGE_SIZE - ((unsigned long)gpio_mem % PAGE_SIZE);

   // Now map it
   gpio_map = mmap(
      (caddr_t)gpio_mem,
      BLOCK_SIZE,
      PROT_READ|PROT_WRITE,
      MAP_SHARED|MAP_FIXED,
      mem_fd,
      GPIO_BASE
   );

   if ((long)gpio_map < 0) {
      printf("mmap error %ld\n", (long)gpio_map);
      exit (-1);
   }

   close (mem_fd);

   // Always use volatile pointer!
   return (volatile unsigned *)gpio_map;
} // setup_io


