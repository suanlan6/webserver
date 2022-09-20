#ifndef LOCKER_H_
#define LOCKER_H_
#include <pthread.h>
#include <semaphore.h>
#include <exception>

// mutex lock
class locker {
public:
    locker() {
        if (pthread_mutex_init(&m_lock, nullptr) != 0) {
            perror("error");
            exit(1);
        }
    }
    
    ~locker() {
        if (pthread_mutex_destroy(&m_lock) != 0) {
            perror("error");
            exit(1);
        }
    }

    bool lock() {
        return pthread_mutex_lock(&m_lock) == 0;
    }

    bool unlock() {
        return pthread_mutex_unlock(&m_lock) == 0;
    }
private:
    pthread_mutex_t m_lock;
};

// conditional variable
class cond {
public:
    cond() {
        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
            perror("error");
            exit(1);
        }
        if (pthread_cond_init(&m_cond, nullptr) != 0) {
            perror("error");
            exit(1);
        }
    }

    ~cond() {
        if (pthread_cond_destroy(&m_cond) != 0) {
            perror("error");
            exit(1);
        }
        if (pthread_mutex_destroy(&m_mutex) != 0) {
            perror("error");
            exit(1);
        }
    }

    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }

    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

    bool wait() {
        return pthread_cond_wait(&m_cond, &m_mutex);
    }

    bool timewait(timespec* order_time) {
        return pthread_cond_timedwait(&m_cond, &m_mutex, order_time);
    }
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

// semaphore
class sem {
public:
    sem() : sem(0, 0) {}
    
    sem(int shared, int value) {
        if (sem_init(&m_sem, shared, value) != 0) {
            perror("error");
            exit(1);
        }
    }

    bool set_sem(int shared, int value) {
        if (sem_init(&m_sem, shared, value) != 0) {
            perror("error");
            return false;
        }
        return true;
    }

    ~sem() {
        if (sem_destroy(&m_sem) !=0) {
            perror("error");
            exit(1);
        }
    }

    bool post() {
        return sem_post(&m_sem) == 0;
    }

    bool wait() {
        return sem_wait(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};
#endif