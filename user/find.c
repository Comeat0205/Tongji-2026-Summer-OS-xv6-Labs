#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

void find(char *path, char *target)
{
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    
    // 打开目录
    if((fd = open(path, O_RDONLY)) < 0){
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }
    
    // 获取文件状态
    if(fstat(fd, &st) < 0){
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }
    
    switch(st.type){
    case T_FILE:
    {
        // 如果是文件，比较文件名
        // 从路径中提取文件名
        char *last_slash = path;
        for(p = path; *p; p++){
            if(*p == '/')
                last_slash = p + 1;
        }
        if(strcmp(last_slash, target) == 0){
            printf("%s\n", path);
        }
        break;
    }
        
    case T_DIR:
    {
        // 如果是目录，递归遍历
        if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
            fprintf(2, "find: path too long\n");
            break;
        }
        
        strcpy(buf, path);
        p = buf + strlen(buf);
        *p++ = '/';
        
        while(read(fd, &de, sizeof(de)) == sizeof(de)){
            if(de.inum == 0)
                continue;
            
            // 跳过 "." 和 ".."
            if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
                continue;
            
            // 构建完整路径
            memmove(p, de.name, DIRSIZ);
            p[DIRSIZ] = 0;
            
            // 递归查找
            find(buf, target);
        }
        break;
    }
    }
    
    close(fd);
}

int main(int argc, char *argv[])
{
    if(argc != 3){
        fprintf(2, "usage: find <directory> <filename>\n");
        exit(1);
    }
    
    find(argv[1], argv[2]);
    exit(0);
}