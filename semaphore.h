#ifndef __SEMAPHORE_H__
#define __SEMAPHORE_H__

#include <mutex>
#include <condition_variable>

using namespace std;

class Semaphore
{
public:
    explicit Semaphore(size_t count = 0) {
        _count = count;
    }

    void signal() {
        unique_lock<mutex> lock(_mutex);
        ++_count;
        _cv.notify_one();
    }

    void wait() {
        unique_lock<mutex> lock(_mutex);
        _cv.wait(lock, [=] {return _count > 0;});
        --_count;
    }

private:
    mutex _mutex;
    condition_variable _cv;
    size_t _count;
};

#endif