
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
  int start;
  int nn;

  dmxdata = open_dmx ("dmxdata");

  start = atoi (argv[1]); 
  char *p = strchr (argv[1], '-');
  if (p) {
     int end = atoi (p+1);
     nn = end-start+1;
  } else 
     nn = 1;
  int d = start+1;
  //printf ("start = %d, nn = %d, d=%d.\n", start, nn, d );
  for (int i = 2;i< argc;i++) 
     for (int j = 0;j<nn;j++)
        dmxdata[d++] = atoi (argv[i]);
  exit (0);
}
