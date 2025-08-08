//
// Created by ivan on 8/2/25.
//

#include "netfunc.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>
#include <sys/sendfile.h>

namespace netfuncs
{

    ioworker::ioworker(int sockfd): m_sockfd(sockfd), deb_info(false) {};

    //Получить тэг и значение
    std::string& ioworker::get_tag(void)
    { return m_tag; }
    std::string& ioworker::get_value(void)
    { return m_value; }

    //Интерфейс
    void ioworker::sendfile(int fd, uint32_t weigth)
    {
        if (deb_info)
            std::cout << "File sending with weight: " << weigth << std::endl;
        iobytes = send_file(m_sockfd, fd, weigth, buffer);
        if (deb_info)
            std::cout << "Sent bytes amount: " << iobytes << std::endl;
        if (iobytes != weigth)
            throw std::runtime_error("sendfile failed");
    }
    void ioworker::recvfile(const std::filesystem::path & filepath)
    {
        iobytes = recv_file(m_sockfd, filepath);
        if(deb_info)
            std::cout << "File " << filepath.string() << " with weight of " << iobytes << " received" << std::endl;
    }
    void ioworker::send(std::string& msg)
    {
        if ((iobytes = sendheader(m_sockfd, msg, buffer)) < 0)
            throw std::runtime_error("Connection is broken");
        if (deb_info)
            std::cout << "Sending: " << m_header << std::endl;
    }

    void ioworker::send(const char* msg)
    {
        if ((iobytes = sendheader(m_sockfd, msg, buffer)) < 0)
            throw std::runtime_error("Connection is broken");
        if (deb_info)
            std::cout << "Sending: " << m_header << std::endl;
    }

    void ioworker::send(std::string& tag, std::string& value)
    {
        m_header = build_header(tag, value);
        if ((iobytes = sendheader(m_sockfd, m_header, buffer)) < 0)
            throw std::runtime_error("Connection is broken");
        if (deb_info)
            std::cout << "Sending: " << m_header << std::endl;
    }

    void ioworker::send(const char* tag, const char* value)
    {
        m_header = build_header(tag, value);
        if ((iobytes = sendheader(m_sockfd, m_header, buffer)) < 0)
            throw std::runtime_error("Connection is broken");
        if (deb_info)
            std::cout << "Sending: " << m_header << std::endl;
    }

    void ioworker::read(void)
    {
        if ((iobytes = recvheader(m_sockfd, m_header, buffer)) < 0)
            throw std::runtime_error("Connection is broken");
        if (deb_info)
            std::cout << "Got: " << m_header << std::endl;
        parse_header(m_header, m_tag, m_value);
    }


    //Сравнения
    bool ioworker::fullcmp(const std::string& tag, const std::string& value)
    {
        if (m_tag == tag && m_value == value)
            return true;
        return false;
    }
    bool ioworker::tagcmp(const std::string& tag)
    {
        if (tag == m_tag)
            return true;
        return false;
    }
    bool ioworker::tagcmp(const char* tag)
    {
        if (tag == m_tag)
            return true;
        return false;
    }
    bool ioworker::valcmp(const std::string& value)
    {
        if (value == m_value)
            return true;
        return false;
    }
    bool ioworker::valcmp(const char* value)
    {
        if (value == m_value)
            return true;
        return false;
    }
    //Настройка
    void ioworker::set_deb_info(bool opt)
    {
        deb_info = opt;
    }


     ascii_string::ascii_string(const std::string & input) : std::string(input)
    { validate(); }

     ascii_string::ascii_string(const char* input) : std::string(input)
    { validate(); }

    void ascii_string::validate() const
    {
        for (unsigned char c : *this)
            if (c > 127)
                throw std::invalid_argument("Wrong ascii_string: " + *this);
    }
;

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

    ssize_t readsocket(const int sockfd, std::vector<char> & buffer, const size_t bytes, const int flags)
    {
        ssize_t readed = recv(sockfd, buffer.data(), bytes, flags);
        if((readed == 0) && (bytes != 0))
            throw std::runtime_error("Connection is broken possibly");
        if(readed == -1)
            throw std::runtime_error("Syscall recv error: " + std::string(strerror(errno)));
        return static_cast<size_t>(readed);
    }

    ssize_t writesocket(const int sockfd, const std::vector<char> & buffer, const size_t bytes, const int flags)
    {
        ssize_t written = send(sockfd, buffer.data(), bytes, flags);
        if((written == 0) && (bytes != 0))
            throw std::runtime_error("Connection is broken possibly");
        if(written == -1)
            throw std::runtime_error("Syscall send error: " + std::string(strerror(errno)));
        return static_cast<ssize_t>(written);
    }

    ssize_t recvheader(const int sockfd, std::string & header, std::vector<char> & buffer)
    {
        uint16_t header_size;
        buffer.resize(2);
        ssize_t bytes_readed = readsocket(sockfd, buffer, 2, 0);
        memcpy(&header_size, buffer.data(), sizeof(header_size));
        header_size = ntohs(header_size);
        if(header_size > BUFFSIZE - 6)
            throw std::runtime_error("Wrong header came");

        buffer.resize(header_size);
        bytes_readed += readsocket(sockfd, buffer, (size_t)header_size, 0);
        header = std::string(buffer.data(), header_size);
        return bytes_readed;
    }

    ssize_t sendheader(const int sockfd, const std::string & header, std::vector<char> & buffer)
    {
        ascii_string ascii_header(header);
        uint16_t header_size = fill_buff(ascii_header, buffer);
        return writesocket(sockfd, buffer, (size_t)header_size, 0);;
    }

    ssize_t send_file(const int sockfd, int fd, uint32_t weight, std::vector<char> & buffer)
    {
        buffer.clear();
        buffer.push_back(static_cast<char>((weight >> 24) & 0xFF));
        buffer.push_back(static_cast<char>((weight >> 16) & 0xFF));
        buffer.push_back(static_cast<char>((weight >> 8) & 0xFF));
        buffer.push_back(static_cast<char>((weight >> 0) & 0xFF)); //Записали вес файла
        ssize_t bytes_readed = send(sockfd, buffer.data(), (size_t)4, MSG_MORE);
        if (bytes_readed != static_cast<size_t>(4))
            throw std::runtime_error("Connection is broken possibly");

        off_t offset = 0;
        while (offset < weight)
        {
            ssize_t sent = sendfile(sockfd, fd, &offset, weight - offset);
            if (sent <= 0) //Ошибка при передаче файла
                throw std::runtime_error("Connection is broken possibly");
        }

        return offset;
    }

    ssize_t recv_file(int sockfd, const std::filesystem::path & filepath)
    {
        //Получаем 4 байта размера файла
        uint32_t file_size_net;
        ssize_t received = recv(sockfd, &file_size_net, sizeof(file_size_net), MSG_WAITALL);
        if (received != sizeof(file_size_net)) {
            throw std::runtime_error("Failed to read file size from socket");
        }
        //Переводим из сетевого порядка в хостовый
        uint32_t file_size = ntohl(file_size_net);
        if (file_size == 0) {
            throw std::runtime_error("Invalid file size (0)");
        }
        // Открываем файл для записи
        std::ofstream outfile(filepath, std::ios::binary);
        if (!outfile.is_open()) {
            throw std::runtime_error("Failed to create file: " + filepath.string());
        }

        // Буфер для чтения
        const size_t buffer_size = 4096;
        char buffer[buffer_size];
        uint32_t total_received = 0;

        while (total_received < file_size) {
            size_t bytes_left = file_size - total_received;
            size_t to_read = std::min(buffer_size, static_cast<size_t>(bytes_left));

            ssize_t n = recv(sockfd, buffer, to_read, 0);
            if (n <= 0) {
                throw std::runtime_error("Socket read error or connection closed");
            }

            outfile.write(buffer, n);
            if (!outfile) {
                throw std::runtime_error("Failed to write to file: " + filepath.string());
            }

            total_received += n;
        }

        outfile.close();
        return total_received;
    }

    //Парсим хэдер на тэг и ценность
    void parse_header(const std::string & header, std::string & tag, std::string & value)
    {
        size_t pos = header.find(':');
        if (pos == std::string::npos)
            throw std::invalid_argument("Invalid header");

        tag = std::string(header.begin(), header.begin() + pos);
        value = std::string(header.begin() + pos + 1, header.end());
    } //Parse header

    std::string build_header(const std::string & tag, const std::string & value)
    {
        std::string temp = tag + ":" + value;
        return temp;
    }

    std::string build_header(const char* tag, const char* value)
    {
        std::string temp = std::string(tag) + ":" + std::string(value);
        return temp;
    }
}
