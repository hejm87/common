#ifndef __COMMON_UTILS_H__
#define __COMMON_UTILS_H__

#include <sys/time.h>
#include <sys/syscall.h>

#include <thread>

template <class T>
class Singleton
{
public:
    static T* instance() {
        static T obj; 
        return &obj;
    }
};

template <class T>
class ThreadSingleton
{
public:
    static T* instance() {
        static thread_local T obj; 
        return &obj;
    }
};

class CommonUtils
{
public:
    inline static long now()
    {
        return time(0); 
    }

    inline static long now_ms()
    {
        struct timeval tv; 
        gettimeofday(&tv, NULL);
        return tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    inline static int gettid()
    {
        return syscall(SYS_gettid); 
    }

    template <class... Args>
    static std::string format_string(const string& fmt, Args... args)
    {
        std::string s;
        auto size = snprintf(NULL, 0, fmt.c_str(), args...) + 1;
        if (s.capacity() < size) {
            s.reserve(size);         
        }
        snprintf((char*)s.c_str(), s.capacity(), fmt.c_str(), args...);
        return std::move(s);
    }

	// 仅日志使用（不具备通用性）
    static std::string date_ms(long time_ms = 0)
    {
        char date[128]; 
        long sec, msec;
        if (time_ms > 0) {
            sec  = time_ms / 1000; 
            msec = time_ms % 1000;
        } else {
            struct timeval tv; 
            gettimeofday(&tv, NULL);
            sec  = tv.tv_sec;
            msec = tv.tv_usec / 1000;
        }

        struct tm tmm;
        localtime_r(&sec, &tmm);
        snprintf(
            date, 
            sizeof(date), 
            "%04d-%02d-%02d_%02d:%02d:%02d.%03ld", 
        	tmm.tm_year + 1900,
        	tmm.tm_mon + 1,
        	tmm.tm_mday,
        	tmm.tm_hour,
        	tmm.tm_min,
        	tmm.tm_sec,
        	msec
        );
        return date;
    }
};

#endif
