
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>


static char *thefile = "dmxfile";
static int offset = 1;
enum dsize_t {DS_SHORT, DS_BYTE, DS_WORD} dsize;

static int dlen[] = {2,1,4};

static void pabort(const char *s)
{
  perror(s);
  exit(1);
}

static const struct option lopts[] = {

  // SPI options. 
  { "file",    1, 0, 'f' },
  { "offset",  1, 0, 'o' },
  { "short",   0, 0, 's' },
  { "byte",    0, 0, 'b' },
  { "word",    0, 0, 'w' },
};


static int parse_opts(int argc, char *argv[])
{
  //  int r;
  int c;

  while (1) {
    
    c = getopt_long(argc, argv, "f:o:sbw", lopts, NULL);
    
    if (c == -1)
      break;
    
    switch (c) {
    case 'f':thefile=strdup (optarg);break;
    case 'o':offset = atoi (optarg);break;
    case 's':dsize = DS_SHORT;break;
    case 'b':dsize = DS_BYTE;break;
    case 'w':dsize = DS_WORD;break;
    }
  }
  return optind;
}


int main (int argc, char **argv)
{
  int nonoptions, i , v, infd;
  char *data;

  nonoptions = parse_opts(argc, argv);
  
  infd = open(thefile, O_RDWR);
  if (infd < 0)
    pabort("can't open dxmfile");

  infd = open (thefile, O_RDWR); 
  if (infd < 0) {
    perror (thefile);
    exit (1);
  }
  data = mmap (NULL, 0x201, PROT_READ | PROT_WRITE, MAP_SHARED, infd, 0);

  if (!data) pabort ("mmap");

  for (i=nonoptions;i<argc;i++) {
    v = strtol (argv[i], NULL, 0);
    switch (dsize) {
    case DS_SHORT:*(uint16_t*)(data+offset) = v;break;
    case DS_BYTE: *( uint8_t*)(data+offset) = v;break;
    case DS_WORD: *(uint32_t*)(data+offset) = v;break;
    }
    offset += dlen [dsize];
  }

  exit (0);
}
