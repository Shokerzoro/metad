#ifndef META_H_INCLUDED
#define META_H_INCLUDED

#include <iostream>
#include <tinyxml2.h>
#include <filesystem>
#include <vector>
#include "net/netfunc.h"
using namespace tinyxml2;
using std::string;
using Direntry = std::filesystem::directory_entry;
using Path = std::filesystem::path;

extern void full_dmeta(XMLElement* parent, Direntry & dir, Path & target); //Генерация full-meta данных
extern void delta_dmeta(XMLElement* oldv, XMLElement* actual, XMLElement* update); //Генерация delta-meta данных
extern void send_delta(XMLElement* xmlel, const Path & filedir, netfuncs::ioworker & unetmes_connector); //Отправка данных по сокету

#endif // META_H_INCLUDED
