#include "kernel/types.h"
#include "kernel/param.h"
#include "kernel/stat.h"
#include "user/user.h"

// Read one line from stdin into buf (up to n-1 bytes).
// Returns number of bytes stored (excluding NUL), 0 on EOF with empty line, -1 on EOF.
int
readline(char *buf, int n)
{
  int i = 0;
  char c;

  while(i < n - 1){
    int r = read(0, &c, 1);
    if(r < 1){
      if(i == 0)
        return -1; // EOF, nothing read
      break;       // EOF after some chars: treat as end of line
    }
    if(c == '\n')
      break;
    buf[i++] = c;
  }
  buf[i] = '\0';
  return i;
}

// Split a line into whitespace-separated arguments.
// Mutates buf by inserting NULs. Returns argc for the appended args.
int
split(char *buf, char **argv, int maxargs)
{
  int n = 0;
  char *p = buf;

  while(*p != '\0' && n < maxargs){
    while(*p == ' ' || *p == '\t')
      p++;
    if(*p == '\0')
      break;
    argv[n++] = p;
    while(*p != '\0' && *p != ' ' && *p != '\t')
      p++;
    if(*p != '\0')
      *p++ = '\0';
  }
  return n;
}

int
main(int argc, char *argv[])
{
  char buf[512];
  char *xargv[MAXARG];
  int i;

  if(argc < 2){
    fprintf(2, "usage: xargs command [args...]\n");
    exit(1);
  }
  if(argc >= MAXARG){
    fprintf(2, "xargs: too many arguments\n");
    exit(1);
  }

  // Fixed command prefix from the command line.
  for(i = 1; i < argc; i++)
    xargv[i - 1] = argv[i];

  while(readline(buf, sizeof(buf)) >= 0){
    int nbase = argc - 1;
    int nadd = split(buf, xargv + nbase, MAXARG - nbase - 1);
    xargv[nbase + nadd] = 0;

    // Empty line: still run the command with only the fixed args
    // (matches common xargs -n 1 behavior for blank lines: skip if no args added)
    if(nadd == 0)
      continue;

    int pid = fork();
    if(pid < 0){
      fprintf(2, "xargs: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec(xargv[0], xargv);
      fprintf(2, "xargs: exec %s failed\n", xargv[0]);
      exit(1);
    }
    wait(0);
  }

  exit(0);
}
