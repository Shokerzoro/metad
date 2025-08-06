//Типовые функции для работы с XML

#ifndef XMLFUNC_H_INCLUDED
#define XMLFUNC_H_INCLUDED

#include <iostream>
#include <tinyxml2.h>
#include <filesystem>

using namespace tinyxml2;
using std::string;
using Path = std::filesystem::path;
using Direntry = std::filesystem::directory_entry;

//Безопасное открытие XML элемента
extern void open_XML_doc(XMLDocument & xmldoc, string fullname);
//Получить версию и удалить файл
extern void get_meta(Path & metafile, string & bldtime_str, string & proj_name, string & vers_str, string & author_str);
//Полчить версию из xml файла
extern void get_version(Path & metaxml_path, std::string & version);
//Добавление элементу типовых атрибутов
extern void set_XML_attr(XMLElement* xmlel, Direntry & dir, Path & target);
//Сравнение элементов по пути (в идеале по относительному имени)
extern bool cmp_XML_path(XMLElement* first, XMLElement* second);
//Сравнение элементов по дате изменений
extern bool cmp_XML_modify(XMLElement* oldv, XMLElement* actual); //сравнивает по хэшу, если совпадают пути

//Добавление атрибуту версионности
//Пока не реализовываем


#endif // XMLFUNC_H_INCLUDED
