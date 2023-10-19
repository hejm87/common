#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include "epoll_executor.h"

EpollEngine::EpollEngine(int thread_count, int max_conn_count)
{
	signal(SIGPIPE, SIG_IGN);
    _max_count = max_conn_count;
	_thread_count = thread_count;
    if (!create_epoll_infos()) {
        throw runtime_error("EpollEngine.create_epoll_infos fail");
    }
    for (int i = 0; i < _thread_count; i++) {
        _threads.emplace_back(thread([this, i] {
            run(i);
        }));
    }
//	printf("EpollEngine|thread.size:%ld\n", _threads.size());
}

EpollEngine::~EpollEngine()
{
    terminate();
}

bool EpollEngine::set(shared_ptr<EpollChannel> chan, int events)
{
	lock_guard<mutex> lock(_mutex);

	auto fd = chan->get_fd();
	auto epoll_id = _epoll_infos[fd % _threads.size()].epoll_id;

	struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
	ev.data.fd = fd;
	if (events & EPOLL_RECV) {
		ev.events |= EPOLLIN;	
	}
	if (events & EPOLL_SEND) {
		ev.events |= EPOLLOUT;
	}

	auto mode = _fd_infos.find(fd) != _fd_infos.end() ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
	if (mode == EPOLL_CTL_ADD && (int)_fd_infos.size() > _max_count) {
		printf("[%d] %s|cur_count:%ld >= _max_count:%d\n", gettid(), __FUNCTION__, _fd_infos.size(), _max_count);
		return false;
	}

	auto ret = epoll_ctl(epoll_id, mode, fd, &ev);
	if (ret == -1) {
		printf(
			"[%d] %s|epoll_ctl fail, epoll_id:%d, fd:%d, mode:%d, error:%s\n", 
			gettid(),
			__FUNCTION__,
			epoll_id,
			fd,
			mode,
            strerror(errno)
		);
		return false;
	}
//	printf("DEBUG|epoll_ctl, ret:%d, epoll_id:%d, fd:%d, events:%d\n", ret, epoll_id, fd, ev.events);

	_fd_infos[fd] = {fd, epoll_id, chan};

	return true;
}

bool EpollEngine::del(shared_ptr<EpollChannel> chan)
{
	int epoll_id;
	{
		lock_guard<mutex> lock(_mutex);
		auto iter = _fd_infos.find(chan->get_fd());
		if (iter == _fd_infos.end()) {
			return false;
		}
		epoll_id = iter->second.epoll_id;
	}

	auto ret = epoll_ctl(epoll_id, EPOLL_CTL_DEL, chan->get_fd(), NULL);
	if (!ret) {
		lock_guard<mutex> lock(_mutex);
		_fd_infos.erase(chan->get_fd());
	}
	return !ret ? true : false;
}

void EpollEngine::terminate()
{
    {
        lock_guard<mutex> lock(_mutex);
        if (_terminate) {
            return ;
        }
        _terminate = true;
    }

	printf("EpollEngine|terminate\n");

    for (auto& item : _epoll_infos) {
        close(item.pipes[0]);
        close(item.pipes[1]);
    }
    for (auto& item : _threads) {
        item.join();
    }

	for (auto& item : _fd_infos) {
		close(item.second.chan->get_fd());
	}
	for (auto& item : _epoll_infos) {
		close(item.epoll_id);
	}
}

int EpollEngine::get_fd_count()
{
	lock_guard<mutex> lock(_mutex);
	return (int)_fd_infos.size();
}

bool EpollEngine::create_epoll_info(EpollInfo& info)
{
    memset(&info, 0, sizeof(info));
    bool flag = false;
    do {
        info.epoll_id = epoll_create(_max_count);
        if (info.epoll_id == -1) {
			printf("%s|epoll_create fail\n", __FUNCTION__);
            break ;
        }
        if (pipe(info.pipes) == -1) {
			printf("%s|pipe fail\n", __FUNCTION__);
            break ;
        }
        info.events = (struct epoll_event*)malloc(_max_count * sizeof(struct epoll_event));
        if (!info.events) {
			printf("%s|malloc events fail\n", __FUNCTION__);
            break ;
        }
        flag = true;
       // printf(
       //     "%s|ptr:%p, epoll_id:%d, pipe[0]:%d, pipe[1]:%d\n", 
       //     __FUNCTION__, 
       //     this, 
       //     info.epoll_id, 
       //     info.pipes[0],
       //     info.pipes[1]
       // );
    } while (0);

    if (!flag) {
        if (info.epoll_id > 0) {
            close(info.epoll_id);
        }
        if (info.pipes[0] > 0) {
            close(info.pipes[0]);
        }
        if (info.pipes[1] > 0) {
            close(info.pipes[1]);
        }
    }
    return flag; 
}

bool EpollEngine::create_epoll_infos()
{
    int i = 0;
    for (; i < _thread_count; i++) {
        EpollInfo info;
        if (!create_epoll_info(info)) {
			printf("%s|create_epoll_info fail, i:%d\n", __FUNCTION__, i);
            break ;
        }
        _epoll_infos.push_back(info);
    }
    if (i != _thread_count) {
        for (auto& item : _epoll_infos) {
            close(item.epoll_id);
            close(item.pipes[0]);
            close(item.pipes[1]);
        }
        _epoll_infos.clear();
        return false;
    }
    return true;
}

void EpollEngine::run(int index)
{
	bool running = true;
	EpollInfo& info = _epoll_infos[index];
	while (running) {
		auto count = epoll_wait(info.epoll_id, info.events, _max_count, -1);
	//	printf("DEBUG|epoll_wait.after, ret:%d\n", count);
		if (count == -1 && errno != EINTR) {
			runtime_error("epoll_wait fail");
		}
		for (int i = 0; i < count; i++) {

			auto& ev = info.events[i];

			auto iter = _fd_infos.find(ev.data.fd);
			assert(iter != _fd_infos.end());

			bool revent = false;
			bool wevent = false;

			// 这里先不考虑EPOLLRDHUP用于处理关闭事件
			if (ev.events & EPOLLERR) {
				revent = wevent = true;
			} else if ((ev.events & EPOLLHUP) && !(ev.events & EPOLLRDHUP)) {
				revent = wevent = true;
			} else if (ev.events & EPOLLIN) {
				revent = true;
			} else if (ev.events & EPOLLOUT) {
				wevent = true;
			}

			if (ev.data.fd == info.pipes[1]) {
				running = false;
				break ;
			} else {
				auto chan = iter->second.chan;
				if (revent && !chan->is_released()) {
					chan->on_recv();
				}
				if (wevent && !chan->is_released()) {
					chan->on_send();
				}
				if (chan->is_released()) {
					del(chan);
				}
			}
		}
	}
}

string EpollEngine::event_desc(int events)
{
	string desc;	
	if (events & EPOLL_RECV) {
		desc += "EPOLL_RECV,";	
	}
	if (events & EPOLL_SEND) {
		desc += "EPOLL_SEND,";	
	}
	if (desc.length() > 0) {
		desc.back() = 0;
	}
	return desc;
}

