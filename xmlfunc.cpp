#include <iostream>
#include <string>
#include <tinyxml2.h>
#include <filesystem>
#include <sys/stat.h>
#include "tstring.h"

using namespace tinyxml2;
using Direntry = std::filesystem::directory_entry;
using Path = std::filesystem::path;
using std::filesystem::relative;
using std::cout;
using std::string;
using std::endl;

//Безопасное открытие XML элемента
void open_XML_doc(XMLDocument & xmldoc, string fullname)
{
    #ifdef DEBUG_BUILD
    cout << "Opening XML doc: " << fullname;
    #endif // debug
    if(xmldoc.LoadFile(fullname.c_str()) != tinyxml2::XML_SUCCESS)
    {
        cout << "Error opening XML doc: " << fullname;
        exit(1);
    }
    #ifdef DEBUG_BUILD
    cout << " - - - complete" << endl;
    #endif // debug
}

void set_XML_attr(XMLElement* xmlel, Direntry& newdirentry, Path& target)
{
    // Получаем time_t время последней перезаписи
    struct stat* statbuf = new struct stat;
    if (stat(newdirentry.path().c_str(), statbuf) != 0)
    {
        cout << "Error statbuf: " << newdirentry.path();
        delete statbuf;
        exit(1);
    }

    time_t modtime = statbuf->st_mtime;
    off_t file_size = statbuf->st_size;  // Получаем размер файла в байтах
    delete statbuf;

    // Заполняем строки атрибутов
    string path = relative(newdirentry.path(), target);
    string weight;
    std::stringstream ss;
    tstring modify(modtime);

    // Формируем атрибуты
    xmlel->SetAttribute("path", path.c_str());
    xmlel->SetAttribute("modify", modify.c_str());
    xmlel->SetAttribute("weight", std::to_string(file_size).c_str());  // Добавляем вес файла
}

//Сравнение элементов по пути (в идеале по относительному)
bool cmp_XML_path(XMLElement* first, XMLElement* second)
{
    const char* firstpath = first->Attribute("path");
    const char* secondpath = second->Attribute("path");
    if(firstpath && secondpath)
    {
        return string(firstpath) == string(secondpath);
    }
    return false;
}

//Сравнение элементов по дате изменений
bool cmp_XML_modify(XMLElement* oldv, XMLElement* actual)
{
    const char* oldm = oldv->Attribute("modify");
    const char* actm = actual->Attribute("modify");
    if(oldm && actm)
        return string(oldm) == string(actm);
    else
        return false;
}
