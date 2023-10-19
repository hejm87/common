#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>

using namespace std;

class Buffer
{
public:
    Buffer(size_t size) {
        _r_pos = 0;
        _w_pos = 0;
        _buffer = (char*)malloc(size);
        if (!_buffer) {
            throw runtime_error("malloc fail");
        }
    }

    ~Buffer() {
        _r_pos = 0;
        _w_pos = 0;
        _size = 0;
        if (_buffer) {
            free(_buffer);
            _buffer = NULL;
        }
    }

    void set(const char* data, size_t size) {
        if (_w_pos + size > size()) {
            move();
        }
        if (used_size() + size > size()) {
            grow();
        }
        memcpy(_buffer + _w_buf, data, size);
        _w_buf += size;
    }

    size_t get(char* data, size_t size) {
        auto ret = get(data, size);
        _r_pos += ret;
        return ret;
    }

    size_t pick(char* data, size_t size) {
        size_t get_size = used_size() > size ? size : used_size();
        if (get_size > 0) {
            memcpy(data, _buffer + _r_pos, get_size);
        }
        return get_size;
    }

    size_t skip(size_t size) {
        size_t skip_size = used_size() > size ? size : used_size();
        if (skip_size > 0) {
            _r_pos += skip_size;
        }
        return skip_size;
    }

    const char* data() {
        return _buffer + _r_pos;
    }

    size_t size() {
        return _data.size();
    }

    size_t used_size() {
        return _w_pos - _r_pos;
    }

    size_t left_size() {
        return size() - used_size();
    }

private:
    void move() {
        if (_r_pos == 0) {
            return ;
        }
        memmove(_buffer, _buffer + _r_pos, _r_pos);
        _w_pos -= _r_pos;
        _r_pos = 0;
    }

    void grow(size_t alloc_size) {
        if (left_size() > alloc_size) {
            return ;
        }
        auto need_size = used_size() + alloc_size;
        auto new_size = _size * 2;
        while (new_size < need_size) {
            new_size *= 2;
        }
        char* new_buffer = (char*)realloc(_buffer, new_size);
        if (!new_buffer) {
            throw runtime_error("realloc fail");
        }
        _size = new_size;
        _buffer = new_buffer;
    }

private:
    size_t  _r_pos;
    size_t  _w_pos;
    size_t  _size;
    char*   _buffer;
};

#endif
