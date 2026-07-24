#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int p[2];
    pipe(p);
    char buf;

    int pid = fork();
    if (pid == 0)
    {
        // 子进程：读父发的字节
        read(p[0], &buf, 1);
        printf("%d: received ping\n", getpid());
        // 回写给父进程
        write(p[1], &buf, 1);
        close(p[0]);
        close(p[1]);
        exit(0);
    }
    else
    {
        // 父进程先发一个字节
        buf = 'x';
        write(p[1], &buf, 1);
        // 等子回复
        read(p[0], &buf, 1);
        printf("%d: received pong\n", getpid());
        close(p[0]);
        close(p[1]);
        wait(0);
        exit(0);
    }
}