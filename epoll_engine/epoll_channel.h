#ifndef __EPOLL_CHANNEL_H__
#define __EPOLL_CHANNEL_H__

#include <memory>
#include <mutex>
#include <sys/syscall.h>

#include "../buffer.h"
#include "../net_utils.h"

using namespace std;

const int RECV_BUF_SIZE = (1024 * 32);

const int DEF_BUFFER_SIZE = 1024;

enum EpollEvent
{
	EPOLL_RECV = 0x1,
	EPOLL_SEND = 0x2,
};

inline int gettid()
{
	return syscall(SYS_gettid);
}

class EpollEngine;

class EpollChannel : public enable_shared_from_this<EpollChannel>
{
friend class EpollEngine;
public:
    EpollChannel(shared_ptr<EpollEngine> engine, int fd, shared_ptr<void> argv = nullptr);

    virtual ~EpollChannel();

	virtual bool init() {return true;}

    virtual void on_send() {;}

	virtual void on_recv() {;}

    virtual void on_close() {;}

    virtual void on_error(int error) {;}

	void release() {_is_released = true;}

    int get_fd() {return _fd;}

	bool is_released() {return _is_released;}

protected:
	void set_fd(int fd) {_fd = fd;}

	bool set_events(int events);

	shared_ptr<EpollEngine> get_engine();

protected:
    int  _fd;
	int  _events;
	bool _is_released;

    Buffer *_w_buf;
    Buffer *_r_buf;

    mutex  _mutex;

    shared_ptr<void> _argv;

    weak_ptr<EpollEngine> _engine;
};

class EpollChannelConnect : public EpollChannel
{
public:
    EpollChannelConnect(shared_ptr<EpollEngine> engine, int fd, shared_ptr<void> argv = nullptr);

	virtual ~EpollChannelConnect() {;}

	virtual bool init();

	virtual void on_send();
	virtual void on_recv();
	virtual void on_recv(const char* data, size_t size);
	virtual void on_close() {;}
	virtual void on_error(int error) {;}

	virtual bool on_message(const string& buffer) = 0;

	virtual int get_packet(const char* data, size_t size, string& buffer) = 0;

	bool send_buffer(const string& data);

protected:
	bool is_ok() {return !is_released() && _is_established;}

	bool _is_established;
};

class EpollChannelClient : public EpollChannelConnect
{
public:
	EpollChannelClient(
		shared_ptr<EpollEngine> engine, 
		const string& host, 
		int port, 
		shared_ptr<void> argv = nullptr
	);

	virtual ~EpollChannelClient() {;}

	bool init();

	void on_send();

	virtual void on_connect() = 0;
	virtual void on_close() {;}
	virtual void on_error(int error) {;}

	virtual bool on_message(const string& buffer) = 0;

	virtual int get_packet(const char* data, size_t size, string& buffer) = 0;

	string get_host() {return _host;}

	int get_port() {return _port;}

private:
	string	_host;	
	int		_port;

	bool	_first_send_event;
};

class EpollChannelServer : public EpollChannel
{
public:
	EpollChannelServer(
		shared_ptr<EpollEngine> engine, 
		const string& host, 
		int port, 
		int backlog, 
		shared_ptr<void> argv = nullptr
	);

	virtual ~EpollChannelServer() {;}

	bool init();

	void on_recv() {
		on_accept();
	}

	string get_host() {return _host;}
	int    get_port() {return _port;}
	int    get_backlog() {return _backlog;}

protected:	
	virtual void on_accept() = 0;

private:
	string	_host;
	int		_port;
	int		_backlog;
};

#endif

