#ifndef THREADDATACONTAINER_H_INCLUDED
#define THREADDATACONTAINER_H_INCLUDED

#include <exception>
#include <stdexcept>
#include <iostream>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>

class ThreadDataContainer
{
private:
    pthread_mutex_t mtx;
    pthread_t m_threadid;
    int m_sockfd;
    bool alive;
public:
    explicit ThreadDataContainer(int sockfd) { pthread_mutex_init(&mtx, nullptr); m_sockfd = sockfd; alive = true; };
    ~ThreadDataContainer(void) { pthread_mutex_destroy(&mtx); }

    int startrun(void) //Вызывается из дочернего потока
    {
        if(pthread_mutex_lock(&mtx) != 0)
            throw std::runtime_error("Mutex locking, limits maybe");
        m_threadid = pthread_self();
        return m_sockfd;
    }
    void* stoprun(void)
    {
        #ifdef DEBUG_BUILD
        std::cout << "Stopping thread: " << m_threadid << std::endl;
        #endif // DEBUG_BUILD
        if (m_sockfd >= 0) // Попытка выключить соединение
        {
            shutdown(m_sockfd, SHUT_RDWR);
            close(m_sockfd);
            m_sockfd = -1; // предотвратить повторное закрытие
        }

        if (pthread_mutex_unlock(&mtx) != 0) // Не удалось разблокировать мьютекс
        { throw std::runtime_error("Mutex unlocking error"); }
        alive = false;

        #ifdef DEBUG_BUILD
        std::cout << "Tread stopped sucssesufly: " << m_threadid << std::endl;
        #endif // DEBUG_BUILD
        return nullptr;
    }
    pthread_t checkrun(void) //Нужна безопасность, если в основном потоке проверка стартанет перед startrun
    {
        if(pthread_mutex_trylock(&mtx) == 0)
        {
            if(!alive)
            { pthread_mutex_unlock(&mtx); return m_threadid; }
            else
            { pthread_mutex_unlock(&mtx); return (pthread_t)-1; } //Непереносимо, и может быть опасно. В линухе работает 1000%
        }
        return (pthread_t)-1;
    }
};

#endif // THREADDATACONTAINER_H_INCLUDED
