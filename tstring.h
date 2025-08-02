#ifndef TSTRING_H_INCLUDED
#define TSTRING_H_INCLUDED

#include <ctime>
#include <string>
#include <sstream>
#include <exception>

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

#endif // TSTRING_H_INCLUDED
