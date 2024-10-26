#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
void doit()
{
    char *args[] = {"/bin/echo", "goodbye", NULL};
    fork();
    execv(args[0], args);
    printf("goodbye\n");
}
int main()
{
    printf("goodbye\n");
    doit();
    printf("goodbye\n");
}
