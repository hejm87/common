#ifndef __EPOLL_EXECUTOR_H__
#define __EPOLL_EXECUTOR_H__

#include <sys/epoll.h>

#include <mutex>
#include <thread>
#include <memory>
#include <functional>

#include <map>
#include <vector>

#include "epoll_channel.h"

using namespace std;

struct EpollInfo
{
    int epoll_id;
    int pipes[2];
    struct epoll_event* events;
};

struct EpollFdInfo
{
	int fd;
    int epoll_id;

	shared_ptr<EpollChannel> chan;
};

class EpollChannel;

class EpollEngine : public enable_shared_from_this<EpollEngine>
{
public:
    EpollEngine(int thread_count, int max_conn_count);
    ~EpollEngine();

    bool set(shared_ptr<EpollChannel> chan, int events = EPOLL_RECV);
    bool del(shared_ptr<EpollChannel> chan);

    void terminate();

	int get_fd_count();

private:
    bool create_epoll_info(EpollInfo& info);
    bool create_epoll_infos();
    
    void run(int index);

	string event_desc(int events);

private:
    bool _terminate;

    int _max_count;

	int _timer_fd;
	int _thread_count;

	map<int, EpollFdInfo> _fd_infos;

    mutex   _mutex;

    vector<thread>      _threads;
    vector<EpollInfo>   _epoll_infos;
};

#endif

