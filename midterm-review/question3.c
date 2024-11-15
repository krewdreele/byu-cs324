#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
int main()
{
    int fd1;
    char c;
    fd1 = open("foobar.txt", O_RDONLY, 0);
    if (fork() == 0)
    {
        read(fd1, &c, 1);
        exit(0);
    }
    sleep(5);
    read(fd1, &c, 1);
    wait(NULL);
    printf("c is %d\n", c);
    exit(0);
}
