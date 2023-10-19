#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdexcept>

using namespace std;

const int RBUFFER_DEF_BLOCK_SIZE = 1024;

class RBuffer
{
public:
    enum {
        DEF_BLOCK_SIZE = 1024,
    };

    RBuffer(size_t def_size = DEF_BLOCK_SIZE) {
		_r_pos = 0;
		_w_pos = 0;
		_used_size = 0;
        _size = def_size;
        _buffer = (char*)malloc(_size);
        if (!_buffer) {
            throw runtime_error("malloc fail");
        }
    }

    ~RBuffer() {
		_r_pos = 0;
		_w_pos = 0;
		_used_size = 0;
        _size = 0;
        free(_buffer);
        _buffer = NULL;
    }

    void set(const char* data, size_t size) {
		if (size > left_size()) {
			grow(size - left_size());
		}
		auto pos  = _w_pos;
		auto left = size;
		if (pos + left >= _size) {
			auto cp_size = _size - pos;
			memcpy(_buffer + pos, data, cp_size);
			left -= cp_size;
			data += cp_size;
			pos = 0;
		}
		memcpy(_buffer + pos, data, left);
		_w_pos = (_w_pos + size) % _size;
		_used_size += size;
	}

    size_t get(char* data, size_t size) {
        auto get_size = pick(data, size);
        if (get_size) {
            _r_pos = (_r_pos + get_size) % _size;
			_used_size -= get_size;
        }
        return get_size;
    }

    size_t pick(char* data, size_t size) {
        size_t get_size = used_size() > size ? size : used_size(); 
        if (get_size > 0) {
			if (_r_pos + get_size > _w_pos && _w_pos > 0) {
				auto cp_size = _size - _r_pos;
				memcpy(data, _buffer + _r_pos, cp_size);
				memcpy(data + cp_size, _buffer, get_size - cp_size);
			} else {
				memcpy(data, _buffer + _r_pos, get_size);
			}
        }
        return get_size;
    }

	bool empty() {
		return _r_pos == _w_pos;
	}

    size_t size() {
        return _size;
    }

    size_t used_size() {
		return _used_size;
    }

    size_t left_size() {
        return size() - used_size();
    }

private:
	void grow(size_t alloc_size) {
		auto new_size = get_new_size(alloc_size + _size);
		char* new_buffer = (char*)realloc(_buffer, new_size);
        if (!new_buffer) {
            throw runtime_error("realloc fail");
        }
		_buffer = new_buffer;
		// Ð´Ö¸ÕëÆ«ÒÆ±È¶ÁÖ¸ÕëÆ«ÒÆÐ¡£¬ÐèÒª×öÇ¨ÒÆ
		if (_used_size > 0 && _w_pos >= _r_pos) {
			memcpy(_buffer + _size, _buffer, _w_pos + 1);
		}
        _w_pos  = _r_pos + used_size();
		_size = new_size;
	}

	size_t get_new_size(size_t size) {
		auto new_size = _size;
		while (new_size < size) {
			new_size *= 2;
		}
		return new_size;
	}

private:
	size_t	_used_size;
    size_t  _size;
    size_t  _r_pos;		// ¿É¶ÁÆ«ÒÆ
    size_t  _w_pos;		// ¿ÉÐ´Æ«ÒÆ
    char*   _buffer;
};

#endif
