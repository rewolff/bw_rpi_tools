
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main (int argc,char **argv)
{
long i,ch;

for (i=1;i<argc;i++)
    {
    //sscanf (argv[i],"%d",&ch);
    ch = strtol (argv[i], NULL, 0);
    putchar (ch);
    }
exit (0);
}
