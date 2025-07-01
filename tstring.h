#ifndef TSTRING_H_INCLUDED
#define TSTRING_H_INCLUDED

#include <ctime>
#include <string>
#include <sstream>
#include <exception>
#include <sys/stat.h>

class tstring : public std::string
{
public:
    tstring(std::time_t & raw_time) : std::string()
    {
        std::tm* fmt_time = std::localtime(&raw_time);
        std::stringstream ss;

        ss << fmt_time->tm_year + 1900 << "-";
        ss << fmt_time->tm_mon + 1 << ".";
        ss << fmt_time->tm_mday << ".";
        ss << fmt_time->tm_hour << ".";
        ss << fmt_time->tm_min;
        ss >> *this;
    }
};

class astring : public std::string
{
public:
    astring(const std::string & input) : std::string(input)
    {
        for (unsigned char c : input)
        {
            if (c > 127) { throw std::invalid_argument("Wrong astring: " + input); }
        }
    }
    astring(const char* input) : std::string(input)
    {
        for (const unsigned char* c = reinterpret_cast<const unsigned char*>(input); *c != '\0'; ++c)
        {
            if (*c > 127) { throw std::invalid_argument("Wrong astring: " + std::string(input)); }
        }
    }
    astring& operator=(const std::string& input)
    {
        for (unsigned char c : input)
        {
            if (c > 127) { throw std::invalid_argument("Wrong astring: " + input); }
        }
        std::string::operator=(input);
        return *this;
    }
    astring& operator=(const char* input)
    {
        for (const unsigned char* c = reinterpret_cast<const unsigned char*>(input); *c != '\0'; ++c)
        {
            if (*c > 127) { throw std::invalid_argument("Wrong astring: " + std::string(input)); }
        }
        std::string::operator=(input);
        return *this;
    }
};

#endif // TSTRING_H_INCLUDED
