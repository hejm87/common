#ifndef __TIMER_H__
#define __TIMER_H__

#include <unistd.h>

#include <map>
#include <unordered_map>
#include <vector>

#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>

#include "any.h"

enum TimerState
{
	TIMER_UNKNOW = -1,
	TIMER_WAIT,
	TIMER_READY,
	TIMER_PROCESS,
	TIMER_FINISH,
	TIMER_CANCEL,
};

const int TIMER_ERROR_TIMERID_INVALID = 1;
const int TIMER_ERROR_TIMERID_NOT_FOUND = 2;
const int TIMER_ERROR_TIMER_CANT_CANCEL = 3;
const int TIMER_ERROR_TIMER_ALREADY_CANCEL = 4;

class Timer;

struct TimerInfo
{
	long				active_time;
	atomic<int>			state;
	function<void()>	func;
};

class TimerId
{
friend class Timer;
public:
	TimerId() = delete;
	TimerId& operator=(const TimerId& id) = delete;

protected:
	TimerId(shared_ptr<TimerInfo> ptr) {
		_ptr = ptr;
	}

	std::shared_ptr<TimerInfo> _ptr;
};

class Timer
{
public:
	typedef std::multimap<long, std::shared_ptr<TimerInfo>>	timer_list_t;
	typedef std::unordered_map<std::shared_ptr<TimerInfo>, timer_list_t::iterator>	map_timer_list_t;

	Timer() {
		_is_init = false;
		_is_set_end = false;
	}

    ~Timer() {
		_is_set_end = true;	
		_cv.notify_all();
		for (auto& item : _threads) {
			item.join();
		}
	}

	void init(size_t thread_num) {
		std::lock_guard<std::mutex> lock(_mutex);
		if (!_is_init) {
			_is_init = true;
			for (int i = 0; i < (int)thread_num; i++) {
				_threads.emplace_back([this]() {
					run();
				});
			}
		}
		return ;
	}

	TimerId set(size_t delay_ms, const std::function<void()>& func) {
		auto ptr = std::shared_ptr<TimerInfo>(new TimerInfo);
		ptr->active_time = now_ms() + (long)delay_ms;
		ptr->func = func;
		ptr->state.store((int)TIMER_WAIT);
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_map_list_iter[ptr] = _list.insert(make_pair(ptr->active_time, ptr));
			TimerId ti(ptr);
		}
		_cv.notify_one();
		return ti;
	}

    int cancel(const TimerId& id) {
		if (!id._ptr) {
			return TIMER_ERROR_TIMERID_INVALID;
		}
		auto state = (TimerState)(id._ptr->state.load());
		if (state == TIMER_PROCESS || state == TIMER_FINISH) {
			return TIMER_ERROR_TIMER_CANT_CANCEL;
		}
		if (state == TIMER_CANCEL) {
			return TIMER_ERROR_TIMER_ALREADY_CANCEL;
		}
		lock_guard<mutex> lock(_mutex);
		auto iter = _map_list_iter.find(id._ptr);
		if (iter == _map_list_iter.end()) {
			return TIMER_ERROR_TIMERID_NOT_FOUND;
		}
		id._ptr->state.store((int)TIMER_CANCEL);
		_list.erase(iter->second);
		_map_list_iter.erase(iter);
		return 0;
	}

	TimerState get_state(const TimerId& id) {
		if (!id._ptr) {
			return TIMER_UNKNOW;
		}
		auto state = (TimerState)(id._ptr->state.load());
		if (state == TIMER_WAIT && id._ptr->active_time <= now_ms()) {
			state = TIMER_READY;
		}
		return state;
	}

	int size() {
		std::lock_guard<std::mutex> lock(_mutex);
		return (int)_list.size();
	}

	bool empty() {
		std::lock_guard<std::mutex> lock(_mutex);
		return _list.size() == 0 ? true : false;	
	}

private:
	void run() {
		while (!_is_set_end) {
			int wait_ms = 0;
			std::shared_ptr<TimerInfo> ptr;
			{
				std::lock_guard<std::mutex> lock(_mutex);
				if (!_list.empty()) {
					auto iter = _list.begin();	
					auto delta = iter->first - now_ms();
					if (delta <= 0) {
						ptr = iter->second;
						_map_list_iter.erase(iter->second);
						_list.erase(iter);
					} else {
						wait_ms = delta;
					}
				}
			}
			if (ptr->func) {
				ptr->_state.store(TIMER_PROCESS);
				ptr->func();
				ptr->_state.store(TIMER_FINISH);
				continue ;
			}
			std::unique_lock<std::mutex> lock(_mutex);
			if (wait_ms == 0) {
				_cv.wait(lock, [this] {
					return !_list.empty() || _is_set_end;
				});
			} else {
				_cv.wait_for(lock, chrono::milliseconds(wait_ms), [this] {
					return !_list.empty() || _is_set_end;
				});
			}
		}
	}

private:
	timer_list_t		_list;
	map_timer_list_t	_map_list_iter;

	std::mutex	_mutex;
	std::condition_variable		_cv;
	std::vector<std::thread>	_threads;

	bool	_is_init;
	bool	_is_set_end;
};

#endif
