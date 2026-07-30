#include "kernel/types.h"
#include "user/user.h"

int main(void)
{
    unsigned int i = 0x726c6400;
    unsigned char *p = (unsigned char *)&i;
    printf("byte0=%x byte1=%x byte2=%x byte3=%x\n",p[0],p[1],p[2],p[3]);
    printf("H%x Wo%s", 57616, &i);
    exit(0);
}
