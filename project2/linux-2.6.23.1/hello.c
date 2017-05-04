#include <stdio.h>
#include <errno.h>
#include "asm/unistd.h"

int main(int argc, char ** argv){
        printf("Calling MY Kernel\n");
        syscall(__NR_hellokernel, 42);
}
