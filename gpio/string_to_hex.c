
#include <stdio.h>
#include <stdlib.h>

int main (int argc, char **argv)
{
   int i, j;
   for (i=1;i<argc;i++) {
      if (i != 1) printf ("20 ");
      for (j=0;argv[i][j];j++)
         printf ("%02x ", 0xff & argv[i][j]);
   }
   printf ("\n");
   exit (0);
}
