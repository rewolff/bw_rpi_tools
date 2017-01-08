
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>


#include <stropts.h>
#include <asm/termios.h>

//#include <sys/ioctl.h>

extern int tcgetattr (int __fd, struct termios *__termios_p) __THROW;
extern void cfmakeraw (struct termios *__termios_p) __THROW;
extern int tcsetattr (int __fd, int __optional_actions,
                      const struct termios *__termios_p) __THROW;



int main (int argc, char **argv)
{
  char *thefile;
  char *data;
  int infd, uartfd;
  char *theuart = "/dev/ttyAMA0";
  struct termios my_tios;
  struct termios2 tio;


  thefile = argv[1];
  infd = open (thefile, O_RDWR); 
  if (infd < 0) {
     perror (thefile);
     exit (1);
  }
  data = mmap (NULL, 0x200, PROT_READ | PROT_WRITE, MAP_SHARED, infd, 0);
 
  uartfd = open (theuart, O_RDWR);
  if (uartfd < 0) {
     perror (theuart);
     exit (1);
  }

  if (tcgetattr(uartfd, &my_tios) < 0) {  // get current settings
      perror ("tcgetattr");
      exit (1);
  } 

  cfmakeraw(&my_tios);  // make it a binary data port
  my_tios.c_cflag |= CLOCAL;    // port is local, no flow control
  my_tios.c_cflag &= ~CSIZE;
  my_tios.c_cflag |= CS8;       // 8 bit chars
  my_tios.c_cflag &= ~PARENB;   // no parity
  my_tios.c_cflag |= CSTOPB;    // 2 stop bit for DMX
  my_tios.c_cflag &= ~CRTSCTS;  // no CTS/RTS flow control

  if (tcsetattr(uartfd, TCSANOW, &my_tios) < 0) {  // apply settings
    perror ("tcsetattr");
    exit (1);
  }
  static const int rate = 250000;

  if (ioctl(uartfd, TCGETS2, &tio) < 0) {
    perror ("tcgets2");
    exit (1);
  }

  tio.c_cflag &= ~CBAUD;
  tio.c_cflag |= BOTHER;
  tio.c_ispeed = rate;
  tio.c_ospeed = rate;  // set custom speed directly
  if (ioctl(uartfd, TCSETS2, &tio) < 0) {
    perror ("tcsets2");
    exit (1);
  }

  while (1) {
    if (ioctl(uartfd, TIOCSBRK, NULL)  < 0) {
       perror ("TIOCCBRK");
       exit (1);
    }
    usleep (100);
    if (ioctl(uartfd, TIOCCBRK, NULL)  < 0) {
       perror ("TIOCSBRK");
       exit (1);
    }
    usleep (10);
    if (write (uartfd, data, 0x200) < 0) {
       perror ("write");
       exit (1);
    }
    usleep (25000);
  }
}
