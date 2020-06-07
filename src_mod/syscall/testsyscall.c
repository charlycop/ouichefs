#include <unistd.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    // biggest
    if(syscall(548, 0) != 0)
    {
        printf("Erreur lors de l'appel du syscall\n");
        return -1;    
    }
    // oldest
    if(syscall(548, 1) != 0)
    {
        printf("Erreur lors de l'appel du syscall\n");
        return -1;    
    }

    return 0;
}

