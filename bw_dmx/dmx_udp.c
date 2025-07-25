/* client.c */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>


#define BUF_SIZE 0x200

int main(int argc, char **argv) 
{
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int sfd, s;
  size_t len;
  char *data;
  char tdata[0x280];
  char *dmxdataname;
  char *port, *host;
  int infd;
  int offset = 0;

  if ((argc > 2) && (strcmp (argv[1], "-o") == 0)) {
    offset = atoi (argv[2]);
    argc -= 2;
    argv += 2;
  }

  if (argc < 2) {
    fprintf(stderr, "Usage: %s host port \n", argv[0]);
    exit(EXIT_FAILURE);
  }
  host = argv[1];
  if (argc > 2) port = argv[2];
  else          port = "6454";

  if (argc > 3) 
    dmxdataname = argv[3];
  else 
    dmxdataname = "dmxdata"; 

  infd = open (dmxdataname, O_RDWR); 
  if (infd < 0) {
    perror (dmxdataname);
    exit (1);
  }

  data = mmap (NULL, 0x201, PROT_READ | PROT_WRITE, MAP_SHARED, infd, 0);
  if (data == NULL) {
    perror ("mmap");
    exit (1);
  }
  
  /* Obtain address(es) matching host/port */

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
  hints.ai_flags = 0;
  hints.ai_protocol = 0;          /* Any protocol */

  s = getaddrinfo(host, port, &hints, &result);
  if (s != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE);
  }

  /* getaddrinfo() returns a list of address structures.
     Try each address until we successfully connect(2).
     If socket(2) (or connect(2)) fails, we (close the socket
     and) try the next address. */

  for (rp = result; rp != NULL; rp = rp->ai_next) {
    sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sfd == -1)
      continue;

    if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
      break;                  /* Success */

    close(sfd);
  }

  if (rp == NULL) {               /* No address succeeded */
    fprintf(stderr, "Could not connect\n");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(result);           /* No longer needed */

  /* Send remaining command-line arguments as separate
     datagrams, and read responses from server */

#define DMXDATAOFFSET 18

  int n = 0;

  while (1) {
    len = 0x200 - offset;
    if ((n > 100) || memcmp (tdata+DMXDATAOFFSET, data+1+offset, len) != 0) {
      // the first byte should be zero indicating "DMX transfer". 
      // The DMX data starts at offset 1. 
      memcpy (tdata+DMXDATAOFFSET, data+1+offset, len);
      len += DMXDATAOFFSET;
      if (write(sfd, tdata, len) != len) {
	perror ("partial/failed write");
	exit(EXIT_FAILURE);
      }
      n = 0;
    } else {
      n++;
    }
    usleep (10000);
  }

  exit(EXIT_SUCCESS);
}
