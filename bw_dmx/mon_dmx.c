
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

int main (int argc, char **argv)
{
  char *thefile;
  unsigned char *data, tdata[0x200];
  int i, infd;

  if (argc > 1) 
     thefile = argv[1];
  else
     thefile = "dmxdata";

  infd = open (thefile, O_RDWR); 
  if (infd < 0) {
     perror (thefile);
     exit (1);
  }

  data = mmap (NULL, 0x201, PROT_READ | PROT_WRITE, MAP_SHARED, infd, 0);
  //  printf ("data=%p.\n", data);
  //dmxmode = DMX_TX;
  while (1) {
    if (memcmp (data, tdata, 0x200) != 0) {
      // the first byte should be zero indicating "DMX transfer". 
      // The DMX data starts at offset 1. 
      for (i=1;i<512;i++)
	printf ("%d,", data[i]);
      printf ("%d\n", data[i]);
      fflush (stdout);
       memcpy (tdata, data, 0x200);
    } 
    usleep (10000);
  }
  exit (0);
}
