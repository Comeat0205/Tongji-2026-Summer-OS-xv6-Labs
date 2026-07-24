#include "kernel/types.h"
#include "user/user.h"

void sieve(int readfd)
{
    int p;
    // 读到0字节流，结束
    if (read(readfd, &p, sizeof(int)) == 0)
    {
        close(readfd);
        exit(0);
    }
    printf("prime %d\n", p);

    int pipefd[2];
    pipe(pipefd);
    int pid = fork();
    if (pid == 0)
    {
        // 子进程继续筛下一轮
        close(pipefd[1]);
        sieve(pipefd[0]);
    }
    else
    {
        // 父过滤能被p整除的数，传给下一级管道
        close(pipefd[0]);
        int n;
        while (read(readfd, &n, sizeof(int)) > 0)
        {
            if (n % p != 0)
            {
                write(pipefd[1], &n, sizeof(int));
            }
        }
        close(readfd);
        close(pipefd[1]);
        wait(0);
        exit(0);
    }
}

int main(void)
{
    int p[2];
    pipe(p);
    int pid = fork();
    if (pid == 0)
    {
        close(p[1]);
        sieve(p[0]);
    }
    else
    {
        close(p[0]);
        // 输入2~35
        for (int i = 2; i <= 35; i++)
        {
            write(p[1], &i, sizeof(int));
        }
        close(p[1]);
        wait(0);
    }
    exit(0);
}