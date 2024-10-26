#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
int main()
{
    int fd1, fd2;
    char c;
    fd1 = open("foobar.txt", O_RDONLY, 0);
    fd2 = open("foobar.txt", O_RDONLY, 0);
    if (fork() == 0)
    {
        read(fd1, &c, 1);
        exit(0);
    }
    read(fd2, &c, 1);
    wait(NULL);
    printf("c = % d\n", c);
    exit(0);
}
