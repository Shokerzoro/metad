#include <iostream>
#include <string>
#include <tinyxml2.h>
#include <filesystem>
#include <sys/stat.h>
#include <exception>
#include "tstring.h"

using namespace tinyxml2;
using Direntry = std::filesystem::directory_entry;
using Path = std::filesystem::path;
using std::filesystem::relative;
using std::runtime_error;
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
//Получить версию и удалить файл
void get_meta(Path & metafile, string & bldtime_str, string & proj_name, string & vers_str, string & author_str)
{
    if(!std::filesystem::exists(metafile))
        throw std::runtime_error("No meta detected");

    #ifdef DEBUG_BUILD
    cout << "Detacted meta.xml: " << metafile << endl;
    #endif

    XMLDocument metaxml;
    metaxml.LoadFile(metafile.string().c_str());
    #ifdef DEBUG_BUILD
    cout << "Meta file loaded: " << metafile.string() << endl;
    #endif

    XMLElement* update;
    XMLElement* project;
    XMLElement* version;
    XMLElement* author;

    if(!(update = metaxml.RootElement()))
        throw std::runtime_error("No root element");
    const char* build_time_c = update->Attribute("build_time");
    if(!build_time_c)
        throw std::runtime_error("No build time");
    bldtime_str = build_time_c;

    if(!(project = update->FirstChildElement("project_name")))
        throw std::runtime_error("No project name element found");
    const char* proj_name_c = project->GetText();
    if(!proj_name_c)
        throw std::runtime_error("Unable to read project name");
    proj_name = proj_name_c;

    if(!(version = update->FirstChildElement("version")))
        throw std::runtime_error("No version element found");
    const char* vers_str_c = version->GetText();
    if(!vers_str_c)
        throw std::runtime_error("Unable to read version");
    vers_str = vers_str_c;

    if(!(author = update->FirstChildElement("author")))
        throw std::runtime_error("No author element found");
    const char* author_str_c = author->GetText();
    if(!author_str_c)
        throw std::runtime_error("Unable to read author");
    author_str = author_str_c;

    cout << "Project " << proj_name << " build at " << bldtime_str << endl;
    cout << "by " << author_str << " has " << vers_str << "version" << endl;

    #ifndef DEBUG_BUILD
    if(!std::filesystem::remove(metafile))
        throw std::runtime_error("Unknown error");
    #endif
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
