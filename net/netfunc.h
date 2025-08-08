//
// Created by ivan on 8/2/25.
//

#ifndef NETFUNC_H
#define NETFUNC_H

#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>  // For C++ code
#include <tinyxml2.h>

namespace netfuncs
{

class ioworker
{
public:
    // Конструктор
    ioworker(int sockfd);
    std::string& get_tag();
    std::string& get_value();

    // Интерфейс
    void sendfile(int fd, uint32_t weigth);
    void recvfile(const std::filesystem::path & filepath);
    void send(std::string& msg);
    void send(const char* msg);
    void send(std::string& tag, std::string& value);
    void send(const char* tag, const char* value);
    void read();

    //Сравнения
    bool fullcmp(const std::string& tag, const std::string& value);
    bool tagcmp(const std::string& tag);
    bool tagcmp(const char* tag);
    bool valcmp(const std::string& value);
    bool valcmp(const char* value);

    // Настройка
    void set_deb_info(bool opt);

private:
    ssize_t iobytes;
    int m_sockfd;
    std::vector<char> buffer;
    std::string m_header, m_tag, m_value;

    bool deb_info;
};

class ascii_string : public std::string
{
public:
    explicit ascii_string(const std::string& input);
    explicit ascii_string(const char* input);

private:
    void validate() const;
};

extern ssize_t send_file(const int sockfd, int fd, uint32_t weight, std::vector<char> & buffer); //Отправка целого файла
extern ssize_t recv_file(int sockfd, const std::filesystem::path & path);
extern ssize_t recvheader(const int sockfd, std::string & header, std::vector<char> & buffer); //Чтение хэдера из сокета
extern ssize_t sendheader(const int sockfd, const std::string & header, std::vector<char> & buffer); //запись хэдера в сокет
extern void parse_header(const std::string & header, std::string & tag, std::string & value); //Парсим хэдер на тэг и ценность
extern std::string build_header(const std::string & tag, const std::string & value);
extern std::string build_header(const char* tag, const char* value);
}


#endif //NETFUNC_H
