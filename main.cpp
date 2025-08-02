#include "meta.h"
#include "tstring.h"
#include "mainfunc.h"
#include "threaddatacontainer.h"

#include <iostream>
#include <string>
#include <filesystem>
#include <exception>
#include <stdexcept>
#include <list>

#include <tinyxml2.h>
#include <pthread.h>
#include <unistd.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace tinyxml2;
using Path = std::filesystem::path;
using Direntry = std::filesystem::directory_entry;
using ThreadList = std::list<ThreadDataContainer*>;

//Глобальные переменные которые лучше не использовать, особенно в многопоточке
Path target;
Path meta;

#ifdef DEBUG_BUILD
extern "C" void __lsan_do_leak_check();
#endif
extern void full_metad_worker(Path & snap_dir, int alrmtime);
extern void* delta_metad_worker(void* data);
extern void become_daemon(std::string logpath);
void crash_reporter(int sig);


int main(int argc, char** argv)
{
    if (((argc == 6) || (argc == 7)) && (strcmp(argv[4], "demonize") == 0))
    {
        
        //Добавим краш-репорты
        signal(SIGABRT, crash_reporter);
        signal(SIGSEGV, crash_reporter);
        signal(SIGBUS, crash_reporter);

        //Необходимые для работы строки
        std::string calltype = argv[1]; //Тип вызова full/delta
        target = Path(argv[2]); //Целевая директория
        meta = Path(argv[3]); //Директория сохранения метаданных
        if (!(calltype == "full" || calltype == "delta"))
            throw std::invalid_argument("Error calltype");
        if(!std::filesystem::exists(target))
            throw std::invalid_argument("Error target directory");
        if(!std::filesystem::exists(meta))
            throw std::invalid_argument("Error meta directory");

        if(calltype == "full") //Демон full-meta
        {
            int alrmtime = -1;
            Path snap_dir(argv[5]);
            if(!std::filesystem::exists(snap_dir))
                std::filesystem::create_directory(snap_dir);

            try {
                if (argv[6] == nullptr)
                    throw std::invalid_argument("No alarm arg");
                alrmtime = std::stoi(argv[6]);
                if((alrmtime <= 0) || (alrmtime > 2*24*60*60))
                    throw std::invalid_argument("Invalid alrm time");
            }
            catch(std::exception & ex)
            {
                std::cout << "alrmtime arg error" << ex.what() << std::endl; exit(1);
            }

            full_metad_worker(snap_dir, alrmtime); //Бесконечная работа демонаъ

		} //Демон full-meta
        if(calltype == "delta") //Демон delta-meta
        {
            #ifndef DEBUG_BUILD
            //Получаем айпи адрес и порт для сокета
            std::string ipv4(argv[5]);
            std::string portstr = argv[6];
            int port = -1;
            try { port = std::stoi(portstr); }
            catch(exception & ex)
            { std::cout << "Port arg error" << ex.what() << std::endl; exit(1); }
            if((port <= 0) || (port > UINT16_MAX))
            { std::cout << "Wrong port num: " << port << std::endl; exit(1); }
            #endif // DEBUG_BUILD
            #ifdef DEBUG_BUILD
            std::string ipv4("127.0.0.1");
            int port = 6666;
            #endif // DEBUG_BUILD

            int serversock, workersock;
            if((serversock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
            { std::cerr << "Socket creation error: " << strerror(errno) << std::endl;
            std::cerr << "Check IPv4: " << ipv4 << std::endl; exit(1); }

            //Создаем сокет, биндим и отмечаем как пассивный
            //Здесь нелбходимо добавить шифрование общения. Будет в конечной версии
            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);
            server_addr.sin_addr.s_addr = inet_addr(ipv4.c_str());
            int opt = 1;

            if (setsockopt(serversock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            { std::cerr << "setsockopt failed: " << strerror(errno) << std::endl; exit(1); }
            if(bind(serversock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) == -1)
            { std::cerr << "Socket binding error: " << strerror(errno) << std::endl; exit(1); }
            if(listen(serversock, 256) == -1)
            { std::cerr << "Socket listening error" << strerror(errno) << std::endl; exit(1); }

            //Создаем сервак и потокобезопасную структуру для обмена инфой
            pthread_t nthreadt;
            int newworkers = 0;
            ThreadList threads;

            std::cout << "Delta metadata configured." << std::endl;
            std::cout << "Started at:" << get_current_time() << std::endl;
            std::cout << "Listening: " << ipv4 << ":" << port << std::endl;

            while(true) //Бесконечный цикл обслуживания клиентов
            {
                #ifdef DEBUG_BUILD
                __lsan_do_leak_check();
                #endif

                workersock = accept(serversock, nullptr, nullptr); //Блокирующее ожидание
                if(workersock < 0) { continue; } //Вдруг было прервано сигналом

                #ifdef DEBUG_BUILD
                std::cout << "Got acception" << std::endl;
                #endif // DEBUG_BUILD

                ThreadDataContainer* nthreadd;
                try {
                    nthreadd = new ThreadDataContainer(workersock);
                }
                catch(std::exception & ex)
                { //Отмена соединения
                    std::cerr << "ThreadDataContainer baddalloc: " << ex.what() << std::endl;
                    shutdown(workersock, SHUT_RDWR);
                    close(workersock);
                    continue;
                }

                if(pthread_create(&nthreadt, nullptr, delta_metad_worker, nthreadd) != 0)
                { //Отмена соединение и освобождение памяти
                    std::cerr << "Thread creation error" << strerror(errno) << std::endl;
                    shutdown(workersock, SHUT_RDWR);
                    close(workersock);
                    delete nthreadd;
                    continue;
                }
                else //Все структуры готовы
                {
                    threads.push_back(nthreadd);
                    newworkers++;
                }

                #ifdef DEBUG_BUILD
                std::cout << "Thread created, thread_id: " << nthreadt << std::endl;
                #endif //DEBUG_BUILD

                for (auto iter = threads.begin(); iter != threads.end();) //Чистим систему от данных
                {
                    ThreadDataContainer* thrd = *iter;
                    pthread_t thrdid = thrd->checkrun();
                    if(thrdid != (pthread_t)-1)
                    { //Присоединяем поток, освобождаем ThreadDataContainer и чистим структуру данных
                        auto deliter = iter; iter++;
                        pthread_join(thrdid, nullptr);
                        threads.erase(deliter);
                        delete thrd;
                    }
                    else
                        iter++;
                }

                #ifdef DEBUG_BUILD
                std::cout << "Waiting next acception" << std::endl;
                #endif // DEBUG_BUILD

            } //Бесконечный цикл обслуживания клиентов
        } //Демон delta-meta
    }//Отработка как демон
    else
    { std::cout << "Error args" << std::endl; }
    return 0;
} //Main

void crash_reporter(int sig)
{
    void* callstack[128];
    int frames = backtrace(callstack, 128);

    std::cerr << "ABORTED. Signal receved: " << sig << std::endl;
    std::cerr << "Backtrace (" << frames << " frames)" << std::endl;

    backtrace_symbols_fd(callstack, frames, STDERR_FILENO);

    _exit(1); // используем _exit, чтобы не вызывать деструкторы
}

