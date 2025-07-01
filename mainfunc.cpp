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

#include "tstring.h"

using namespace tinyxml2;
using std::string;
using std::cout;
using std::endl;
using std::copy;
using Path = std::filesystem::path;
using Direntry = std::filesystem::directory_entry;
using Diriter = std::filesystem::directory_iterator;

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
    int logfile = open(logpath.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (logfile == -1)
        throw std::runtime_error("Error open logfile");

    // Закрытие стандартных дескрипторов и перенаправление stdout и stderr в лог
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    dup2(logfile, STDOUT_FILENO);
    dup2(logfile, STDERR_FILENO);
    close(logfile); //Можно убрать, чтобы не занимал места
}

void mute_signals(void)
{
    sigset_t new_mask;
    sigfillset(&new_mask);
    if(pthread_sigmask(SIG_SETMASK, &new_mask, nullptr) == -1)
      throw std::runtime_error("Signals block error: " + string(strerror(errno)));
}

Path get_actual(const Path & dir_path, string & actualdate)
{
    #ifdef DEBUG_BUILD
    cout << "Searching actual XML doc" << endl;
    #endif

    Path latest_file;
    struct statx statxbuf;
    time_t latest_btime = 0;

    for (const auto& entry : Diriter(dir_path))
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

    actualdate = tstring(latest_btime);
    return latest_file;
}

string get_current_time(void)
{
    std::time_t raw_time = std::time(nullptr);
    tstring timestr(raw_time);
    return timestr;
}

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

extern size_t recvheader(const int sockfd, std::string & header, std::vector<char> & buffer)
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

extern size_t sendheader(const int sockfd, const std::string & header, std::vector<char> & buffer)
{
    uint16_t header_size = fill_buff(header, buffer);
    return writesocket(sockfd, buffer, (size_t)header_size, 0);;
}
