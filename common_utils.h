#ifndef __COMMON_H__
#define __COMMON_H__

#include <sys/syscall.h>

inline int gettid()
{
    return syscall(SYS_gettid);
}

#endif
