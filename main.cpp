#include <iostream>
#include <string>
#include <filesystem>
#include <tinyxml2.h>
#include <exception>
#include <list>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <execinfo.h>
#include <stdlib.h>
#include <signal.h>

#include "meta.h"
#include "tstring.h"
#include "xmlfunc.h"
#include "mainfunc.h"
#include "threaddatacontainer.h"

using namespace tinyxml2;
using std::string;
using std::cout;
using std::cerr;
using std::endl;
using std::exception;
using Path = std::filesystem::path;
using Direntry = std::filesystem::directory_entry;
using ThreadList = std::list<ThreadDataContainer*>;

extern void full_metad_worker(Path & snap_dir, int alrmtime);
extern void* delta_metad_worker(void* data);
extern void become_daemon(string logpath);
void crash_reporter(int sig);
//Общие для всех переменные
Path target;
Path meta;

int main(int argc, char** argv)
{
    if (((argc == 6) || (argc == 7)) && (strcmp(argv[4], "demonize") == 0))
    {
        //Добавим краш-репорты
        signal(SIGABRT, crash_reporter);
        signal(SIGSEGV, crash_reporter);
        signal(SIGBUS, crash_reporter);

        //Необходимые для работы строки
        string calltype = argv[1]; //Тип вызова full/delta
        target = Path(argv[2]); //Целевая директория
        if(!std::filesystem::exists(target))
        { cout << "Error target directory: " << target << endl; exit(1); }
        meta = Path(argv[3]); //Директория сохранения метаданных
        if(!std::filesystem::exists(meta))
        { cout << "Error meta directory: " << target << endl; exit(1); }

        if(calltype == "full") //Демон full-meta
        {
            Path snap_dir(argv[5]);
            if(!std::filesystem::exists(snap_dir))
                std::filesystem::create_directory(snap_dir);

            string alrm_str = argv[6];
            int alrmtime = -1;
            try { alrmtime = std::stoi(alrm_str); }
            catch(exception & ex)
            { cout << "alrmtime arg error" << ex.what() << endl; exit(1); }
            if((alrmtime <= 0) || (alrmtime > 2*24*60*60))
            { cout << "Wrong port alrmtime: " << alrmtime << endl; exit(1); }

            full_metad_worker(snap_dir, alrmtime); //Бесконечная работа демонаъ

		} //Демон full-meta
        if(calltype == "delta") //Демон delta-meta
        {
            #ifndef DEBUG_BUILD
            //Получаем айпи адрес и порт для сокета
            string ipv4(argv[5]);
            string portstr = argv[6];
            int port = -1;
            try { port = std::stoi(portstr); }
            catch(exception & ex)
            { cout << "Port arg error" << ex.what() << endl; exit(1); }
            if((port <= 0) || (port > UINT16_MAX))
            { cout << "Wrong port num: " << port << endl; exit(1); }
            #endif // DEBUG_BUILD
            #ifdef DEBUG_BUILD
            string ipv4("127.0.0.1");
            int port = 6666;
            #endif // DEBUG_BUILD

            int serversock, workersock;
            if((serversock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
            { cerr << "Socket creation error: " << strerror(errno) << endl;
            cerr << "Check IPv4: " << ipv4 << endl; exit(1); }

            //Создаем сокет, биндим и отмечаем как пассивный
            //Здесь нелбходимо добавить шифрование общения. Будет в конечной версии
            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);
            server_addr.sin_addr.s_addr = inet_addr(ipv4.c_str());
            int opt = 1;

            if (setsockopt(serversock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
            { cerr << "setsockopt failed: " << strerror(errno) << endl; exit(1); }
            if(bind(serversock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) == -1)
            { cerr << "Socket binding error: " << strerror(errno) << endl; exit(1); }
            if(listen(serversock, 256) == -1)
            { cerr << "Socket listening error" << strerror(errno) << endl; exit(1); }

            //Создаем сервак и потокобезопасную структуру для обмена инфой
            pthread_t nthreadt;
            int newworkers = 0;
            ThreadList threads;

            cout << "Delta metadata configured." << endl;
            cout << "Started at:" << get_current_time() << endl;
            cout << "Listening: " << ipv4 << ":" << port << endl;

            while(true) //Бесконечный цикл обслуживания клиентов
            {
                workersock = accept(serversock, nullptr, nullptr); //Блокирующее ожидание
                if(workersock < 0) { continue; } //Вдруг было прервано сигналом

                #ifdef DEBUG_BUILD
                cout << "Got acception" << endl;
                #endif // DEBUG_BUILD

                ThreadDataContainer* nthreadd;
                try {
                    nthreadd = new ThreadDataContainer(workersock);
                }
                catch(exception & ex)
                { //Отмена соединения
                    cerr << "ThreadDataContainer baddalloc: " << ex.what() << endl;
                    shutdown(workersock, SHUT_RDWR);
                    close(workersock);
                    continue;
                }

                if(pthread_create(&nthreadt, nullptr, delta_metad_worker, nthreadd) != 0)
                { //Отмена соединение и освобождение памяти
                    cerr << "Thread creation error" << strerror(errno) << endl;
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
                cout << "Thread created, thread_id: " << nthreadt << endl;
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
                cout << "Waiting next acception" << endl;
                #endif // DEBUG_BUILD

            } //Бесконечный цикл обслуживания клиентов
        } //Демон delta-meta
    }//Отработка как демон
    else
    { cout << "Error args" << endl; }
    return 0;
} //Main

void crash_reporter(int sig)
{
    void* callstack[128];
    int frames = backtrace(callstack, 128);

    std::cerr << "ABORTED. Signal receved: " << sig << endl;
    std::cerr << "Backtrace (" << frames << " frames)" << endl;

    backtrace_symbols_fd(callstack, frames, STDERR_FILENO);

    _exit(1); // используем _exit, чтобы не вызывать деструкторы
}

