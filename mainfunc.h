#ifndef MAINFUNC_H_INCLUDED
#define MAINFUNC_H_INCLUDED

#include <iostream>
#include <filesystem>
#include <map>
#include "tstring.h"

using Path = std::filesystem::path;
using Pair = std::pair<int, std::string>;
using IMap = std::map<int, std::string>;
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
extern int clear_mapper(IMap & mapper);
extern void inoupdate(int infd, IMap & mapper);

//Работа с файлами и строками
extern void get_actual(const Path & dir_path, Path & actual_meta_path); //Получение актуального файла
extern std::string computeFileSHA256(const std::string& filePath); //Получение хэша sha256 по файлу
extern std::string get_current_time(void) noexcept;

#endif // MAINFUNC_H_INCLUDED
