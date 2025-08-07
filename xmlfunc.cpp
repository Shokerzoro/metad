#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <tinyxml2.h>
#include <sys/stat.h>

#include "tstring.h"
#include "mainfunc.h"

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
//Получить метаданные
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
    cout << "by " << author_str << " has " << vers_str << " version " << endl;
}

//Полчить версию из xml файла
extern void get_version(Path & metaxml_path, std::string & version)
{
    XMLDocument metaxml;
    XMLError result = metaxml.LoadFile(metaxml_path.string().c_str());
    if (result != tinyxml2::XML_SUCCESS)
        throw std::runtime_error("Unable to load metaxml");

    XMLElement* update = metaxml.RootElement();
    if (!update)
        throw std::runtime_error("No root element");

    const char* version_c = update->Attribute("version");
    if (!version_c)
        throw std::runtime_error("No version attribute found");

    version = version_c;
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
    string fullfilepath = newdirentry.path();
    string path = relative(fullfilepath, target);
    string weight = std::to_string(file_size);
    std::stringstream ss;
    tstring modify(modtime);

    // Формируем атрибуты
    xmlel->SetAttribute("path", path.c_str());
    xmlel->SetAttribute("modify", modify.c_str());
    xmlel->SetAttribute("weight", weight.c_str());  // Добавляем вес файла

    #ifdef DEBUG_BUILD
    std::cout << "New element set attributes: " << std::endl;
    std::cout << "Path: " << path << std::endl;
    std::cout << "Modified: " << modify << std::endl;
    std::cout << "Weight: " << weight << std::endl;
    #endif

    //Добавляем хэш, если это файл
    if (newdirentry.is_regular_file())
    {
        std::string hash_string = computeFileSHA256(fullfilepath);
        xmlel->SetAttribute("sha256", hash_string.c_str());
        #ifdef DEBUG_BUILD
        std::cout << "Hash: " << hash_string << std::endl;
        #endif
    }
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

//Сравнение элементов (файлов)
bool cmp_XML_modify(XMLElement* oldv, XMLElement* actual)
{
    if (!oldv || !actual)
        throw std::invalid_argument("Null pointer passed to cmp_XML_modify.");

    const char* oldm = oldv->Attribute("modify");
    const char* actm = actual->Attribute("modify");
    const char* oldhash = oldv->Attribute("sha256");
    const char* acthash = actual->Attribute("sha256");

    if (!oldm || !actm || !oldhash || !acthash)
        throw std::invalid_argument("Missing required XML attributes for comparison.");

    return (string(oldm) == string(actm)) || (string(oldhash) == string(acthash));
}
