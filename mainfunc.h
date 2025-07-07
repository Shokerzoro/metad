#ifndef MAINFUNC_H_INCLUDED
#define MAINFUNC_H_INCLUDED

#include <iostream>
#include <filesystem>
#include <vector>
#include "tstring.h"
using std::string;
using Path = std::filesystem::path;

extern void become_daemon(void);
extern void mute_signals(void);

extern Path get_actual(const Path& dir_path, string & actualdate);
extern string get_current_time(void);

//Заполнит 2 первые байта буфера размером строки, и дополнит строкой. Если встретяться не ascii символы выкинет invalid argument. Вернет общее кол-во
extern uint16_t fill_buff(const astring & input, std::vector<char> & buffer);
//Ввод/вывод из сокета в буфер (нет особого смысла теперь в этих функциях, там только обработка событий отсоединения и все. Может немного безопаснее)
extern size_t readsocket(const int sockfd, std::vector<char> & buffer, const size_t bytes, const int flags = 0);
extern size_t writesocket(const int sockfd, const std::vector<char> & buffer, const size_t bytes, const int flags = 0);
//Чтение/запись хэдера из сокета
extern size_t recvheader(const int sockfd, std::string & header, std::vector<char> & buffer);
extern size_t sendheader(const int sockfd, const std::string & header, std::vector<char> & buffer);

#endif // MAINFUNC_H_INCLUDED
