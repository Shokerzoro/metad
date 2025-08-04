//
// Created by ivan on 8/2/25.
//

#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>
#include <sys/sendfile.h>

class ascii_string : public std::string
{
public:
    explicit ascii_string(const std::string & input) : std::string(input)
    { validate(); }
    explicit ascii_string(const char* input) : std::string(input)
    { validate(); }

private:
    void validate() const
    {
        for (unsigned char c : *this)
            if (c > 127)
                throw std::invalid_argument("Wrong ascii_string: " + *this);
    }
};

// - - - - - - - - - - - - - - Работа с сетью - - - - - - - - - - - - - -
uint16_t fill_buff(const ascii_string & input, std::vector<char> & buffer)
{
    long unsigned headlength = input.length(); //Кол-во символов без терминального нуля
    auto filled = static_cast<uint16_t>(headlength);
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
        throw std::runtime_error("Syscall recv error: " + std::string(strerror(errno)));
    return static_cast<size_t>(readed);
}

size_t writesocket(const int sockfd, const std::vector<char> & buffer, const size_t bytes, const int flags)
{
    ssize_t written = send(sockfd, buffer.data(), bytes, flags);
    if((written == 0) && (bytes != 0))
        throw std::runtime_error("Connection is broken possibly");
    if(written == -1)
        throw std::runtime_error("Syscall send error: " + std::string(strerror(errno)));
    return static_cast<size_t>(written);
}

size_t recvheader(const int sockfd, std::string & header, std::vector<char> & buffer)
{
    uint16_t header_size;
    size_t bytes_readed = readsocket(sockfd, buffer, 2, 0);
    memcpy(&header_size, buffer.data(), sizeof(header_size));
    header_size = ntohs(header_size);
    if(header_size > BUFFSIZE - 6)
        throw std::runtime_error("Wrong header came");

    bytes_readed += readsocket(sockfd, buffer, (size_t)header_size, 0);
    header = std::string(buffer.data(), header_size);
    return bytes_readed;
}

size_t sendheader(const int sockfd, const std::string & header, std::vector<char> & buffer)
{
    ascii_string ascii_header(header);
    uint16_t header_size = fill_buff(ascii_header, buffer);
    return writesocket(sockfd, buffer, (size_t)header_size, 0);;
}

size_t send_file(const int sockfd, int fd, uint32_t weight, std::vector<char> & buffer)
{
        buffer.clear();
        buffer.push_back(static_cast<char>((weight >> 24) & 0xFF));
        buffer.push_back(static_cast<char>((weight >> 16) & 0xFF));
        buffer.push_back(static_cast<char>((weight >> 8) & 0xFF));
        buffer.push_back(static_cast<char>((weight >> 0) & 0xFF)); //Записали вес файла
        ssize_t bytes_readed = send(sockfd, buffer.data(), (size_t)4, MSG_MORE);
        if (bytes_readed != static_cast<size_t>(4))
            throw std::runtime_error("Connection is broken possibly");

        //Отправляем файл целиком
        #ifndef DEBUG_BUILD
        off_t offset = 0;
        while (offset < weight)
        {
            ssize_t sent = sendfile(sockfd, fd, &offset, weight - offset);
            if (sent <= 0) //Ошибка при передаче файла
                throw std::runtime_error("Connection is broken possibly");
        }
        #endif // DEBUG_BUILD

        return bytes_readed;
}

std::string build_header(const char* tag, const char* value)
{
    std::string temp = std::string(tag) + ":" + std::string(value);
    return temp;
}
