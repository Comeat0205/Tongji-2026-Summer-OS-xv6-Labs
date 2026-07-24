#include "kernel/types.h"
#include "user/user.h"
#include "kernel/stat.h"

#define MAXARGS 16
#define BUFSIZE 128

// 按换行分割一行字符串，填入argv数组，返回参数个数
int split(char *buf, char **argv) {
    int n = 0;
    char *p = buf;
    while (*p != '\0' && n < MAXARGS - 1) {
        // 跳过空格
        while (*p == ' ') p++;
        if (*p == '\0' || *p == '\n') break;
        argv[n++] = p;
        // 走到下一个分隔符
        while (*p != ' ' && *p != '\n' && *p != '\0') p++;
        if (*p != '\0') *p++ = '\0';
    }
    argv[n] = 0;
    return n;
}

int main(int argc, char *argv[]) {
    // 没有传入命令，直接报错退出
    if (argc < 2) {
        fprintf(2, "xargs: need command arguments\n");
        exit(1);
    }

    char buf[BUFSIZE];
    char *exec_argv[MAXARGS];

    while (1) {
        // 复制原有命令到执行参数数组
        int arg_cnt = 0;
        for (int i = 1; i < argc; i++) {
            exec_argv[arg_cnt++] = argv[i];
        }

        // 读取一行输入
        int len = read(0, buf, BUFSIZE - 1);
        if (len <= 0) break; // EOF 结束循环
        buf[len] = '\0';

        // 分割当前行的参数，追加到执行列表
        char *split_args[MAXARGS];
        int n = split(buf, split_args);
        for (int i = 0; i < n && arg_cnt < MAXARGS - 1; i++) {
            exec_argv[arg_cnt++] = split_args[i];
        }
        exec_argv[arg_cnt] = 0;

        // fork 执行命令
        int pid = fork();
        if (pid == 0) {
            exec(exec_argv[0], exec_argv);
            // exec 失败会走到这里
            fprintf(2, "xargs: exec %s failed\n", exec_argv[0]);
            exit(1);
        } else {
            wait(0); // 父进程等待子进程结束
        }
    }

    exit(0);
}