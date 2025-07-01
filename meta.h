#ifndef META_H_INCLUDED
#define META_H_INCLUDED

#include <iostream>
#include <tinyxml2.h>
#include <filesystem>
#include <vector>
using namespace tinyxml2;
using std::string;
using Direntry = std::filesystem::directory_entry;
using Path = std::filesystem::path;

extern void full_dmeta(XMLElement* parent, Direntry & dir, Path & target); //Генерация full-meta данных
extern void delta_dmeta(XMLElement* oldv, XMLElement* actual, XMLElement* update); //Генерация delta-meta данных
extern void send_delta(int sockfd, XMLElement* xmlel, const string & filedir, std::vector<char> & buffer); //Отправка данных по сокету

#endif // META_H_INCLUDED
