//
// Created by ivan on 8/2/25.
//

#ifndef UNACCAPTABLE_H
#define UNACCAPTABLE_H

#include <exception>
#include <stdexcept>
#include <string>

//Исключение, которое должно приводить к завершению программы / потока, как правиль
class Unacceptable : public std::runtime_error
{
public:
    Unacceptable(const std::string & msg) : std::runtime_error(msg) {};
};

#endif //UNACCAPTABLE_H
