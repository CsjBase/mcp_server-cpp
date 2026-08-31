#include "net/base/Thread.h"
#include "utils/os.h"

#include <semaphore.h>

namespace net
{
    std::atomic_int Thread::s_numCreated(0);

    Thread::Thread(ThreadFunc func, const std::string &name)
        : m_started(false), m_joined(false), m_tid(0), m_func(std::move(func)), m_name(name)
    {
        setDefaultName();
    }

    Thread::~Thread()
    {
        if (m_started && !m_joined)
        {
            m_thread->detach(); // thread类提供的设置分离线程的方法
        }
    }

    void Thread::start() // 一个Thread对，记录的就是一个新线程的详细信息
    {
        sem_t sem;
        sem_init(&sem, false, 0);
        m_started = true;
        // 开启线程
        m_thread = std::make_unique<std::thread>([&]()
                                                 {
                                                     // 获取线程的tid值
                                                     m_tid = utils::thread_id();
                                                     sem_post(&sem);
                                                     m_func(); // 开启一个新线程，专门执行该线程函数
                                                 });

        // 等待获取上面新创建的线程的tid值
        sem_wait(&sem);
    }
    void Thread::join()
    {
        m_joined = true;
        m_thread->join();
    }
    void Thread::setDefaultName()
    {
        int num = ++s_numCreated;
        if (m_name.empty())
        {
            char buf[32] = {0};
            snprintf(buf, sizeof buf, "Thread%d", num);
        }
    }
}