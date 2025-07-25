

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

char *open_dmx (char *fname)
{
  int infd; 
  char *data;

  infd = open (fname, O_RDWR); 
  if (infd < 0) {
    perror (fname);
    exit (1);
  }

  data = mmap (NULL, 0x201, PROT_READ | PROT_WRITE, MAP_SHARED, infd, 0);
  if (data == NULL) {
    perror ("mmap");
    exit (1);
  }
  return data;
}

int main (int argc, char **argv) 
{
  char *dmxdata;

  dmxdata = open_dmx ("dmxdata");

  while (1) {
    for (int i=0;i<64;i++)
      dmxdata[1+i] = random ();
    usleep (25000);
  }
  exit (0);
}
