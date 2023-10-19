#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "epoll_channel.h"
#include "epoll_executor.h"

int get_socket_error(int fd)
{
	int err;
	socklen_t len = sizeof(err);
	auto ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
	return ret == 0 ? err : -1;
}

EpollChannel::EpollChannel(shared_ptr<EpollEngine> engine, int fd, shared_ptr<void> argv)
{
	_fd = fd;
	_argv   = argv;
	_engine = engine;

	_is_released = false;

	_w_buf = new Buffer(DEF_BUFFER_SIZE);
	_r_buf = new Buffer(DEF_BUFFER_SIZE);
}

EpollChannel::~EpollChannel()
{
	if (_fd != -1) {
		close(_fd);	
		_fd = -1;
	}
	delete _w_buf;
	delete _r_buf;
}

bool EpollChannel::set_events(int events)
{
	return get_engine()->set(shared_from_this(), events);
}
    
shared_ptr<EpollEngine> EpollChannel::get_engine()
{
	auto e = _engine.lock();
	if (!e) {
		throw runtime_error("get_engine, EpollEngine invalid");
	}
	return e;
}

EpollChannelConnect::EpollChannelConnect(shared_ptr<EpollEngine> engine, int fd, shared_ptr<void> argv)
:EpollChannel(engine, fd, argv)
{
	_is_established = false;
}

bool EpollChannelConnect::init()
{
	auto ret = set_events(EPOLL_RECV);
	if (ret) {
		_is_established = true;
	}
	return ret;
}

void EpollChannelConnect::on_send()
{
	lock_guard<mutex> lock(_mutex);
	if (!is_ok()) {
		return ;
	}
	if (!_w_buf->used_size()) {
		set_events(EPOLL_RECV);
		return ;
	}
	auto send_size = _w_buf->used_size();
	auto ret = send(_fd, _w_buf->data(), send_size, 0);
	if (ret > 0) {
		if (ret == send_size) {
		//	printf("[%d] %s|send is complete, fd:%d\n", gettid(), __FUNCTION__, get_fd());
			set_events(EPOLL_RECV);
		}
		_w_buf->skip(ret);
	} else if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
		if (errno == EPIPE) {
			printf("[%d] %s|channel.close, fd:%d\n", gettid(), __FUNCTION__, get_fd());
			on_close();
		} else {
			auto err = get_socket_error(get_fd());
			printf("[%d] %s|channel.error, fd:%d, err:%d\n", gettid(), __FUNCTION__, get_fd(), err);
			on_error(errno);
		}
		release();
	}
}

void EpollChannelConnect::on_recv()
{
	{
		lock_guard<mutex> lock(_mutex);
		if (!is_ok()) {
			return ;
		}
	}

	char buf[RECV_BUF_SIZE];
	auto ret = recv(_fd, buf, sizeof(buf), 0);
	if (ret > 0) {
		on_recv(buf, ret);
	} else {
		if (ret == 0) {
			printf("[%d] %s|channel.close, fd:%d\n", gettid(), __FUNCTION__, get_fd());
			on_close();
		} else {
			auto err = get_socket_error(get_fd());
			printf("[%d] %s|channel.error, fd:%d, err:%d\n", gettid(), __FUNCTION__, get_fd(), err);
			on_error(err);
		}
		release();
	}
}

void EpollChannelConnect::on_recv(const char* data, size_t size)
{
	{
		lock_guard<mutex> lock(_mutex);
		if (!is_ok()) {
			return ;
		}
		_r_buf->set(data, size);
	}
	while (1) {
		string buffer;
		{
			lock_guard<mutex> lock(_mutex);
			auto ret = get_packet(_r_buf->data(), _r_buf->used_size(), buffer);
			if (ret > 0) {
				_r_buf->skip(ret);
			} else {
				break ;
			}
		}
		on_message(buffer);
	}
}

bool EpollChannelConnect::send_buffer(const string& data)
{
	{
		lock_guard<mutex> lock(_mutex);
		if (!is_ok()) {
			printf("%s|fd:%d, goto not ok\n", __FUNCTION__, get_fd());
			return false;
		}
	}
	if (data.length() > 0) {
		{
			auto old_size = _w_buf->size();
			lock_guard<mutex> lock(_mutex);
			_w_buf->set(data.c_str(), data.length());
			if (!set_events(EPOLL_SEND | EPOLL_RECV)) {
				_w_buf->truncate(old_size);
				return false;
			}
		}
	}
	return true;
}

EpollChannelClient::EpollChannelClient(
	shared_ptr<EpollEngine> engine, 
	const string& host, 
	int port, 
	shared_ptr<void> argv
) : EpollChannelConnect(engine, -1, argv)
{
	_host = host;
	_port = port;
	_argv = argv;

	_first_send_event = true;
}

bool EpollChannelClient::init()
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		printf("%s|socket fail, error:%s\n", __FUNCTION__, strerror(errno));
		return false;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(_port);
	addr.sin_addr.s_addr = inet_addr(_host.c_str());

	auto ret = NetUtils::set_socket_unblock(fd);
	if (ret == -1) {
		printf(
			"%s|set_socket_unblock fail, fd:%d, error:%s", 
			__FUNCTION__,
			fd,
			strerror(errno)
		);
		close(fd);
		return false;
	}

	ret = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1 && errno != EINPROGRESS) {
		printf(
			"%s|connect fail, fd:%d, host:%s, port:%d, errno:%d, error:%s\n", 
			__FUNCTION__,
			fd,
			_host.c_str(),
			_port,
			errno,
			strerror(errno)
		);
		close(_fd);
		return false;
	}

	set_fd(fd);

	if (!set_events(EPOLL_SEND | EPOLL_RECV)) {
		printf("%s|set_events fail, event:EPOLL_RECV, fd:%d\n", get_fd());
		return false;
	}

	return true;
}

void EpollChannelClient::on_send()
{
	{
		lock_guard<mutex> lock(_mutex);
		if (is_released()) {
			return ;
		}
	}

	if (!_is_established) {
		_is_established = true;
		on_connect();
		set_events(EPOLL_SEND | EPOLL_RECV);
	} else {
		EpollChannelConnect::on_send();
	}
}

EpollChannelServer::EpollChannelServer(
	shared_ptr<EpollEngine> engine, 
	const string& host, 
	int port, 
	int backlog, 
	shared_ptr<void> argv
) : EpollChannel(engine, -1, argv)
{
	_host = host;
	_port = port;
	_backlog = backlog;
}

bool EpollChannelServer::init()
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(_port);

	if (_host.length() > 0) {
		addr.sin_addr.s_addr = inet_addr(_host.c_str());
	} else {
		addr.sin_addr.s_addr = INADDR_ANY;
	}

	auto fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		printf("%s|socket fail, error:%s\n", __FUNCTION__, strerror(errno));
		return false;
	}

	int ret = -1;
	do {
		ret = NetUtils::set_socket_reuseaddr(fd);
		if (ret == -1) {
			printf(
				"%s|set_socket_reuseaddr fail, fd:%d, error:%s\n", 
				__PRETTY_FUNCTION__, 
				get_fd(), 
				strerror(errno)
			);
			break ;
		}

		ret = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
		if (ret == -1) {
			printf(
				"%s|bind fail, fd:%d, error:%s\n", 
				__PRETTY_FUNCTION__, 
				get_fd(), 
				strerror(errno)
			);
			break ;
		}

		ret = listen(fd, _backlog);
		if (ret == -1) {
			printf(
				"%s|listen fail, fd:%d, error:%s\n", 
				__PRETTY_FUNCTION__, 
				get_fd(), 
				strerror(errno)
			);
			break ;
		}

	} while (0);

	if (ret != 0) {
		close(fd);
	} else {
		set_fd(fd);
		if (!set_events(EPOLL_RECV)) {
			printf("%s|events_set fail, fd:%d, flag:EPOLL_RECV\n", __PRETTY_FUNCTION__, fd);
			ret = -1;
		}
	}
	return !ret ? true : false;
}

