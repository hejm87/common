#ifndef __NET_UTILS_H__
#define __NET_UTILS_H__

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>

class NetUtils
{
public:
    static int set_socket_block(int fd) {
        auto flag = fcntl(fd, F_GETFL, 0);
        if (flag == -1) {
            return -1;
        }
        flag &= ~O_NONBLOCK;
        return fcntl(fd, F_SETFL, flag);
    }

    static int set_socket_unblock(int fd) {
        auto flag = fcntl(fd, F_GETFL, 0);
        if (flag == -1) {
            return -1;
        }
        return fcntl(fd, F_SETFL, flag | O_NONBLOCK);
    }

    static int set_socket_reuseaddr(int fd, bool on = true) {
        int opt = on ? 1 : 0;
        return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void*)&opt, sizeof(opt));
    }
};

#endif
