#include "kernel/types.h"
#include "user/user.h"

int main(void)
{
    unsigned int i = 0x00646c72;
    printf("H%x Wo%s", 57616, &i);
    exit(0);
}
