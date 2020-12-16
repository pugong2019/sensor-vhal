#ifndef CG_MUTEX_H
#define CG_MUTEX_H

#include <mutex>
class Mutex {
public:
    void lock() { m_mutex.lock(); }
    void unlock() { m_mutex.unlock(); }
    bool tryLock() { return m_mutex.try_lock(); }

private:
    std::mutex m_mutex;
};

class AutoMutex {
public:
    explicit AutoMutex(Mutex& mutex)
        : m_mutex(mutex)
    {
        m_mutex.lock();
    };

    ~AutoMutex()
    {
        m_mutex.unlock();
    };

private:
    Mutex& m_mutex;
};
#endif