#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  if(argc != 2){
    printf("usage: sleep ticks\n");
    exit(1);
  }
  int t = atoi(argv[1]);
  sleep(t);
  exit(0);
}