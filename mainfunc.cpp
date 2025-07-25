#include <iostream>
#include <filesystem>
#include <exception>
#include <tinyxml2.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <cstdint>
#include <algorithm>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <vector>
#include <sys/inotify.h>
#include <sys/sendfile.h>
#include <map>

#include "tstring.h"

#define DIRMASK (IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_ONLYDIR)
#define FILEMASK (IN_MODIFY | IN_DELETE_SELF | IN_DONT_FOLLOW)

using namespace tinyxml2;
using std::string;
using std::cout;
using std::endl;
using std::copy;
using Path = std::filesystem::path;
using Direntry = std::filesystem::directory_entry;
using Diriter = std::filesystem::directory_iterator;
using Pair = std::pair<int, string>;
using IMap = std::map<int, string>;

static uint16_t fill_buff(const astring & input, std::vector<char> & buffer);
static size_t readsocket(const int sockfd, std::vector<char> & buffer, const size_t bytes, const int flags = 0);
static size_t writesocket(const int sockfd, const std::vector<char> & buffer, const size_t bytes, const int flags = 0);

// - - - - - - - - - - - - - - Работа с потоками и сигналами - - - - - - - - - - - - - -
void become_daemon(string logpath)
{
    pid_t pid = fork();
    if (pid < 0)
        throw std::runtime_error("Fork failed");
    if (pid > 0)
        exit(0); // Родитель завершает работу

    // Создание новой сессии
    if (setsid() < 0)
        throw std::runtime_error("New session creation failed");

    pid = fork();
    if (pid < 0)
        throw std::runtime_error("Fork failed");
    if (pid > 0)
        exit(0); // Второй родитель завершает работу

    // Права доступа по умолчанию
    umask(0);

    // Изменяем рабочую директорию
    if (chdir("/") < 0)
        throw std::runtime_error("Change dir failed");

    // Открытие файла лога
    int logfile = open(logpath.c_str(), O_CREAT | O_WRONLY, 0644);
    if (logfile == -1)
        throw std::runtime_error("Error open logfile");

    // Закрытие стандартных дескрипторов и перенаправление stdout и stderr в лог
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    dup2(logfile, STDOUT_FILENO);
    dup2(logfile, STDERR_FILENO);
    close(logfile); //Можно убрать, чтобы не занимал места

    #ifdef DEBUG_BUILD
    cout << "Demon started" << endl;
    #endif // DEBUG_BUILD
}

void mute_signals(void)
{
    sigset_t new_mask;
    sigfillset(&new_mask);
    if(pthread_sigmask(SIG_SETMASK, &new_mask, nullptr) == -1)
      throw std::runtime_error("Signals block error: " + string(strerror(errno)));
}

void setup_sigalarm_handler(void (*handler)(int))
{
    struct sigaction sa {};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGALRM, &sa, nullptr) == -1) {
        cout << "ALARM sigaction error: " << strerror(errno) << endl;
        exit(1);
    }
}

// - - - - - - - - - - - - - - Работа с системой Inotify - - - - - - - - - - - - - -
int inoinit(void)
{
    int infd = inotify_init();
    if (infd == -1)
        throw std::runtime_error("inotify init error");
    return infd;
}

int watch_path(int infd, Direntry & newentry, uint32_t mask)
{
    #ifdef DEBUG_BUILD
    cout << "new watching request: " << newentry.path().string() << endl;
    #endif
    int retvalue = inotify_add_watch(infd, newentry.path().string().c_str(), mask);
    #ifdef DEBUG_BUILD
    if (retvalue == -1)
    {
        cout << "Add watch error: " << newentry.path().string() << endl;
        perror("inotify_add_watch");
    } else
    {
        cout << "Added new inotify object: " << newentry.path().string() << endl;
    }
    #endif
    return retvalue;
}

void inotify_loop(int infd, Path & target, IMap & mapper) noexcept
{
    #ifdef DEBUG_BUILD
    cout << "Inotify request: " << target.string() << endl;
    #endif
    Direntry newdir(target);
    if (newdir.is_directory() && !newdir.is_symlink())
    {
        int newfd = watch_path(infd, newdir, DIRMASK);
        if (newfd != -1 && mapper.find(newfd) == mapper.end())
        {
            #ifdef DEBUG_BUILD
            cout << "Its new directory" << endl;
            #endif
            mapper.insert(Pair(newfd, target.string()));
        }

        for (Diriter curriter(target); curriter != end(curriter); curriter++)
        {
            if (curriter->is_regular_file() && !curriter->is_symlink())
            {
                Direntry newfile(*curriter);
                #ifdef DEBUG_BUILD
                cout << "New entry is file: " << newfile.path().string() << endl;
                #endif
                int newfd = watch_path(infd, newfile, FILEMASK);
                if (newfd != -1 && mapper.find(newfd) == mapper.end()) {
                    mapper.insert(Pair(newfd, (*curriter).path().string()));
                }
            }
            if (curriter->is_directory() && !curriter->is_symlink())
            {
                Path newpath((*curriter).path());
                Direntry newdirloop(newpath);
                #ifdef DEBUG_BUILD
                cout << "New entry is directory: " << newdirloop.path().string() << endl;
                #endif
                inotify_loop(infd, newpath, mapper);
            }
        }
    }
}

int clear_mapper(IMap & mapper)
{
    int cleared = 0;
    auto iter = mapper.begin();
    while (iter != mapper.end())
    {
        auto nextiter = std::next(iter);
        if (!std::filesystem::exists(iter->second))
        {
            mapper.erase(iter);
            cleared++;
        }
        iter = nextiter;
    }
    return cleared;
}

// - - - - - - - - - - - - - - Работа с файлами и строками - - - - - - - - - - - - - -
void get_actual(const Path & meta_dir, Path & actual_meta_path, string & actualdate)
{
    #ifdef DEBUG_BUILD
    cout << "Searching actual XML doc" << endl;
    #endif

    Path latest_file;
    struct statx statxbuf;
    time_t latest_btime = 0;

    for (const auto& entry : Diriter(meta_dir))
    {
        if (entry.is_regular_file() && !entry.is_symlink())
        {
            int fd = open(entry.path().c_str(), O_RDONLY);
            if (fd == -1)
            { continue; }

            if (statx(fd, "", AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW, STATX_BTIME, &statxbuf) == -1)
            {
                close(fd);
                continue;
            }

            time_t btime = statxbuf.stx_btime.tv_sec;
            if (btime > latest_btime)
            {
                latest_btime = btime;
                latest_file = entry.path();
            }
            close(fd);
        }
    }
    actual_meta_path = latest_file;
    actualdate = tstring(latest_btime);
}

string get_current_time(void)
{
    std::time_t raw_time = std::time(nullptr);
    tstring timestr(raw_time);
    return timestr;
}

// - - - - - - - - - - - - - - Работа с сетью - - - - - - - - - - - - - -
uint16_t fill_buff(const astring & input, std::vector<char> & buffer)
{
    long unsigned headlength = input.length(); //Кол-во символов без терминального нуля
    uint16_t filled = static_cast<uint16_t>(headlength);
    if(headlength > BUFFSIZE - 6)
        throw std::invalid_argument("Too big msg to header"); //4 байта для размера тела

    buffer.clear();
    buffer.push_back(static_cast<char>((filled >> 8) & 0xFF));
    buffer.push_back(static_cast<char>((filled >> 0) & 0xFF));  // Записываем в обратном порядке байтов
    for(auto iter = input.begin(); iter != input.end(); iter++)
        buffer.push_back(*iter);

    return filled+2;
}

size_t readsocket(const int sockfd, std::vector<char> & buffer, const size_t bytes, const int flags)
{
    ssize_t readed = recv(sockfd, buffer.data(), bytes, flags);
    if((readed == 0) && (bytes != 0))
        throw std::runtime_error("Connection is broken possibly");
    if(readed == -1)
        throw std::runtime_error("Syscall recv error: " + string(strerror(errno)));
    return static_cast<size_t>(readed);
}

size_t writesocket(const int sockfd, const std::vector<char> & buffer, const size_t bytes, const int flags)
{
    ssize_t written = send(sockfd, buffer.data(), bytes, flags);
    if((written == 0) && (bytes != 0))
        throw std::runtime_error("Connection is broken possibly");
    if(written == -1)
        throw std::runtime_error("Syscall send error: " + string(strerror(errno)));
    return static_cast<size_t>(written);
}

size_t recvheader(const int sockfd, std::string & header, std::vector<char> & buffer)
{
    uint16_t header_size;
    size_t ioctl = readsocket(sockfd, buffer, 2, 0);
    memcpy(&header_size, buffer.data(), sizeof(header_size));
    header_size = ntohs(header_size);
    if(header_size < BUFFSIZE - 6)
        throw std::runtime_error("Wrong header came");

    ioctl += readsocket(sockfd, buffer, (size_t)header_size, 0);
    header = string(buffer.data(), header_size);

    return ioctl;
}

size_t sendheader(const int sockfd, const std::string & header, std::vector<char> & buffer)
{
    uint16_t header_size = fill_buff(header, buffer);
    return writesocket(sockfd, buffer, (size_t)header_size, 0);;
}

size_t send_file(const int sockfd, int fd, uint32_t weight, std::vector<char> & buffer)
{
        buffer.clear();
        buffer.push_back(static_cast<char>((weight >> 24) & 0xFF));
        buffer.push_back(static_cast<char>((weight >> 16) & 0xFF));
        buffer.push_back(static_cast<char>((weight >> 8) & 0xFF));
        buffer.push_back(static_cast<char>((weight >> 0) & 0xFF)); //Записали вес файла
        int ioctl = send(sockfd, buffer.data(), (size_t)4, MSG_MORE);

        //Отправляем файл целиком
        #ifndef DEBUG_BUILD
        off_t offset = 0;
        while (offset < weight)
        {
            ssize_t sent = sendfile(sockfd, fd, &offset, weight - offset);
            if (sent <= 0) //Ошибка при передаче файла
                return -1;
        }
        #endif // DEBUG_BUILD

        return ioctl;
}
