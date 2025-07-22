#ifndef MAINFUNC_H_INCLUDED
#define MAINFUNC_H_INCLUDED

#include <iostream>
#include <filesystem>
#include <vector>
#include <map>
#include "tstring.h"

using std::string;
using Path = std::filesystem::path;
using Pair = std::pair<int, string>;
using IMap = std::map<int, string>;
using Direntry = std::filesystem::directory_entry;
using Diriter = std::filesystem::directory_iterator;

//Работа с потоками и сигналами
extern void become_daemon(void);
extern void mute_signals(void);
extern void setup_sigalarm_handler(void (*handler)(int));

//Работа с системой Inotify
extern int inoinit(void);
extern int watch_path(int infd, Direntry & newentry, uint32_t mask);
extern void inotify_loop(int infd, Path & target, IMap & mapper);
extern void inoupdate(int infd, IMap & mapper);

//Работа с файлами и строками
extern void get_actual(const Path & dir_path, Path & actual_meta_path, string & actualdate); //Получение актуального файла
extern string get_current_time(void) noexcept;

//Работа с сетью
extern size_t send_file(const int sockfd, Path & file); //Отправка целого файла
extern size_t recvheader(const int sockfd, std::string & header, std::vector<char> & buffer); //Чтение хэдера из сокета
extern size_t sendheader(const int sockfd, const std::string & header, std::vector<char> & buffer); //запись хэдера в сокет

#endif // MAINFUNC_H_INCLUDED
