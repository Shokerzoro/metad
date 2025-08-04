//
// Created by ivan on 8/2/25.
//

#ifndef NETFUNC_H
#define NETFUNC_H

#include <string>
#include <vector>

struct TagStrings {
    static constexpr const char* PROTOCOL = "PROTOCOL";
    static constexpr const char* UNETMES = "UNET-MES";
    static constexpr const char* NOUPDATE = "NOUPDATE";
    static constexpr const char* SOMEUPDATE = "SOMEUPDATE";
    static constexpr const char* GETUPDATE = "GETUPDATE";
    static constexpr const char* VERSION = "VERSION";
    static constexpr const char* HASH = "HASH";

    static constexpr const char* NEWDIR = "NEWDIR";
    static constexpr const char* NEWFILE = "NEWFILE";
    static constexpr const char* DELFILE = "DELFILE";
    static constexpr const char* DELDIR = "DELDIR";

    static constexpr const char* AGREE = "AGREE";
    static constexpr const char* REJECT = "REJECT";
    static constexpr const char* COMPLETE = "COMPLETE";
};

class ascii_string
{
public:
    ascii_string(const std::string & input);
    ascii_string(const char* input);
    ascii_string & operator=(const std::string& input);
    ascii_string & operator=(const char* input);
};

//Работа с сетью
extern size_t send_file(const int sockfd, int fd, uint32_t weight, std::vector<char> & buffer); //Отправка целого файла
extern size_t recvheader(const int sockfd, std::string & header, std::vector<char> & buffer); //Чтение хэдера из сокета
extern size_t sendheader(const int sockfd, const std::string & header, std::vector<char> & buffer); //запись хэдера в сокет
extern void parse_header(const std::string & header, std::string & tag, std::string & value); //Парсим хэдер на тэг и ценность
extern std::string build_header(const char* tag, const char* value);

#endif //NETFUNC_H
