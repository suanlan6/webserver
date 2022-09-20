#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>
#include <pthread.h>
#include <list>
#include <assert.h>

#include "locker.hpp"

/* thread pool*/
template<typename T>
class thread_pool {
public:
    thread_pool(int thread_num = 8, int max_tasks = 10000);
    ~thread_pool() { delete[] m_threads; is_stop = true; }
    bool append_task(T* task);

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator =(const thread_pool&) = delete;
private:
    static void* work_process(void* arg);
    void run();
private:

    // stop or not
    bool is_stop;

    // thread nums
    int m_thread_num;

    // max tasks
    int m_max_tasks;
    
    // task queue
    std::list<T*> m_task_queue;

    // mutex locker
    locker m_locker;
    
    // semapore
    sem m_sem;

    // threads
    pthread_t* m_threads;
};

/* thread_pool constructor */
template<typename T>
thread_pool<T>::thread_pool(int thread_num, int max_tasks) 
    : is_stop(false), m_thread_num(thread_num), m_max_tasks(max_tasks),
     m_threads(nullptr) {
        m_sem.set_sem(0, thread_num);
        assert(thread_num > 0 && max_tasks > 0);
        m_threads = new pthread_t[thread_num];
        assert(m_threads);

        printf("thread num: %d\n", thread_num);
        int ret;
        for (int i = 0; i < thread_num; ++i) {
            ret = pthread_create(&m_threads[i], nullptr, work_process, this);
            if (ret != 0) {
                delete[] m_threads;
                perror("error while creating thread pool");
                exit(1);
            }

            // detach while creating
            ret = pthread_detach(m_threads[i]);
            if (ret != 0) {
                delete[] m_threads;
                perror("error while depatching the threads");
                exit(1);
            }
            printf("create %d thread\n", i);
        }
}

/* work_process func */
template<typename T>
void* thread_pool<T>::work_process(void* arg) {
    thread_pool* pool = (thread_pool*)arg;
    pool->run();
    return pool;
}

/* append work*/
template<typename T>
bool thread_pool<T>::append_task(T* task) {
    m_locker.lock();
    if (m_task_queue.size() == m_max_tasks) {
        m_locker.unlock();
        return false;
    }
    m_task_queue.push_back(task);
    m_locker.unlock();
    m_sem.post();
    return true;
}

/* thread main func */
template<typename T>
void thread_pool<T>::run() {
    while(!is_stop) {
        m_sem.wait();
        m_locker.lock();
        if (m_task_queue.empty()) {
            m_locker.unlock();
            continue;
        }
        T* task = m_task_queue.front();
        m_task_queue.pop_front();
        m_locker.unlock();
        if (!task)
            continue;
        task->process();
    }
}
#endif