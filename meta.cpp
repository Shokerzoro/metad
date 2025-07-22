#include <iostream>
#include <filesystem>
#include <tinyxml2.h>
#include <exception>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <vector>

#include "xmlfunc.h"
#include "mainfunc.h"
#include "tstring.h"

using namespace tinyxml2;
using std::string;
using std::cout;
using std::cerr;
using std::endl;
using Direntry = std::filesystem::directory_entry;
using Diriter = std::filesystem::directory_iterator;
using Path = std::filesystem::path;
using std::exception;

void add_news(XMLElement* update, XMLElement* actual);

void full_dmeta(XMLElement* parent, Direntry & dir, Path & target)
{
    //Создаем итератор, и проходим цикл пока он не будет равен последнему итератору
    try {
    Diriter curriter(dir);

    while(curriter != end(curriter))
    {
        Direntry newdirentry(*curriter); //Выделение памяти
        //Если итератор указывает на каталог, добавляем XML документ с аттрибутами и вызываем рекурсию
        if(newdirentry.is_directory() && !newdirentry.is_symlink())
        {
            #ifdef DEBUG_BUILD
            cout << newdirentry.path() << ":   " << "directory" << endl;
            #endif // debug

            //Запись XML элемента - директории

            XMLElement* newdirelement = parent->InsertNewChildElement("directory");
            set_XML_attr(newdirelement, newdirentry, target);

            //Рекурсия
            full_dmeta(newdirelement, newdirentry, target);

        }
        //Если это файл, добавляем запись и идем дальше
        else if(newdirentry.is_regular_file() && !newdirentry.is_symlink())
        {
            #ifdef DEBUG_BUILD
            cout << newdirentry.path() << ":   " << "file" << endl;
            #endif // debug

            //Запись XML элемента - файла
            XMLElement* newfileelement = parent->InsertNewChildElement("file");
            set_XML_attr(newfileelement, newdirentry, target);
        }
        curriter++;
    }
    }
    catch(exception & ex)
    {
        cout << "Error: " << ex.what() << endl;
        return;
    }
}

//Принимает указатеи на элементы, которые являются "каталогами"
void delta_dmeta(XMLElement* oldv, XMLElement* actualv, XMLElement* update)
{
    // Инициализация итераторов
    XMLElement* oldviter = oldv->FirstChildElement("file"); //Элемент из старого XML документа
    XMLElement* actualviter = actualv->FirstChildElement("file"); //Элемент из актуального

    //Отработка только файлов
    while(oldviter && actualviter)
    {
        // Случай совпадения путей
        if(cmp_XML_path(oldviter, actualviter))
        {
            // Если время модификации отличается
            if(!cmp_XML_modify(oldviter, actualviter))
            {
                // Создаем новый элемент обновления идеальный вариант
                XMLElement* newfile = update->InsertNewChildElement("newfile");
                const char* newpath = actualviter->Attribute("path");
                const char* weigth = actualviter->Attribute("weight");
                newfile->SetAttribute("path", newpath);
                newfile->SetAttribute("weight", weigth);
            }
            // Переходим к следующим элементам
            oldviter = oldviter->NextSiblingElement("file");
            actualviter = actualviter->NextSiblingElement("file");
        }
        // Случай несовпадения путей
        else {
            // Ищем файл в актуальной версии
            XMLElement* curriter = actualviter;
            while(curriter && !cmp_XML_path(oldviter, curriter))
            {
                curriter = curriter->NextSiblingElement("file");
            }

            // Если файл не найден в актуальной версии
            if(!curriter)
            {
                // Создаем элемент удаления
                XMLElement* delfile = update->InsertNewChildElement("delfile");
                const char* delpath = oldviter->Attribute("path");
                delfile->SetAttribute("path", delpath);
                oldviter = oldviter->NextSiblingElement("file");
            }
            // Если файл найден, но между actualviter и curriter есть новые файлы
            else
            {
                // Отработка добавленные файлы
                XMLElement* temp = actualviter;
                while(temp != curriter)
                {
                    XMLElement* newfile = update->InsertNewChildElement("newfile");
                    const char* newpath = temp->Attribute("path");
                    const char* weigth = temp->Attribute("weight");
                    newfile->SetAttribute("path", newpath);
                    newfile->SetAttribute("weight", weigth);
                    temp = temp->NextSiblingElement("file");
                }
                // Возвращаемся к обработке текущего файла
                actualviter = curriter->NextSiblingElement("file");
            }
        }
    }

    // Отработка оставшихся файлов в oldviter (удаленные файлы)
    while(oldviter)
    {
        XMLElement* delfile = update->InsertNewChildElement("delfile");
        const char* delpath = oldviter->Attribute("path");
        delfile->SetAttribute("path", delpath);
        oldviter = oldviter->NextSiblingElement("file");
    }

    // Отработка оставшихся файлов в actualviter (новые файлы)
    while(actualviter)
    {
        XMLElement* newfile = update->InsertNewChildElement("newfile");
        const char* newpath = actualviter->Attribute("path");
        const char* weigth = actualviter->Attribute("weight");
        newfile->SetAttribute("path", newpath);
        newfile->SetAttribute("weight", weigth);
        actualviter = actualviter->NextSiblingElement("file");
    }

    // Обработка каталогов с рекурсией
    XMLElement* olddir = oldv->FirstChildElement("directory");
    XMLElement* newdir = actualv->FirstChildElement("directory");

    while (olddir && newdir)
    {
        const char* oldpath = olddir->Attribute("path");
        const char* newpath = newdir->Attribute("path");

        if (strcmp(oldpath, newpath) == 0)
        {
            // Пути совпадают — рекурсивно сравниваем содержимое
            // Временно создаём подэлемент <directory>
            XMLElement* updatedir = update->InsertNewChildElement("directory");
            updatedir->SetAttribute("path", oldpath);

            // Рекурсивный вызов
            delta_dmeta(olddir, newdir, updatedir);

            // Если в updatedir ничего не добавлено — удаляем его
            if (!updatedir->FirstChildElement())
            {
                update->DeleteChild(updatedir);
            }

            olddir = olddir->NextSiblingElement("directory");
            newdir = newdir->NextSiblingElement("directory");
        } else
        {
            // Поиск совпадающего каталога в actualv
            XMLElement* searcher = newdir;
            while (searcher && strcmp(oldpath, searcher->Attribute("path")) != 0)
            {
                searcher = searcher->NextSiblingElement("directory");
            }

            if (!searcher)
            {
                // Каталог удалён
                XMLElement* deldir = update->InsertNewChildElement("deldir");
                deldir->SetAttribute("path", oldpath);
                olddir = olddir->NextSiblingElement("directory");
            }
            else
            {
                // Добавленные каталоги до найденного
                XMLElement* temp = newdir;
                while (temp != searcher)
                {
                    XMLElement* newd = update->InsertNewChildElement("newdir");
                    newd->SetAttribute("path", temp->Attribute("path"));
                    temp = temp->NextSiblingElement("directory");
                }
                newdir = searcher->NextSiblingElement("directory");
                olddir = olddir->NextSiblingElement("directory");
            }
        }
    }

    // Удалённые каталоги
    while (olddir)
    {
        XMLElement* deldir = update->InsertNewChildElement("deldir");
        deldir->SetAttribute("path", olddir->Attribute("path"));
        olddir = olddir->NextSiblingElement("directory");
    }

    // Добавленные каталоги
    while (newdir) //Элемент, который мы парсим
    {
        XMLElement* newd = update->InsertNewChildElement("newdir"); //Новый элемент в update
        newd->SetAttribute("path", newdir->Attribute("path"));

        // Обработка файлов в директории
        add_news(newd, newdir);

        newdir = newdir->NextSiblingElement("directory");
    }
}

//Отправка данных, при парсинге файла delta-meta
void send_delta(int sockfd, XMLElement* xmlel, const string & filedir, std::vector<char> & buffer)
{
    XMLElement* xmliter = nullptr;
    uint16_t header_size;

    xmliter = xmlel->FirstChildElement("newfile");
    while(xmliter) //Цикл отправки файлов
    {
        string tag("NEWFILE:");
        string relpath = xmliter->Attribute("path");
        string weightstr = xmliter->Attribute("weight");
        string fullname = filedir + relpath;
        string header = tag + relpath;
        if (!std::filesystem::exists(fullname))
            throw std::runtime_error("Файл не найден: " + fullname);

        int fd = -1; //Дескриптор файла для sendfile
        char* endptr = nullptr;
        uint32_t weight = std::strtoll(weightstr.c_str(), &endptr, 10);

        try { //Отправляем заголовок
            if(weight == 0)
                throw std::invalid_argument("Symlink maybe. How can it get here?");
            if((fd = open(fullname.c_str(), O_RDONLY)) == -1)
                throw std::runtime_error("Ошибка открытия файла: " + fullname);

            header_size = fill_buff(header, buffer); //Записали хэдер
            buffer.push_back(static_cast<char>((weight >> 24) & 0xFF));
            buffer.push_back(static_cast<char>((weight >> 16) & 0xFF));
            buffer.push_back(static_cast<char>((weight >> 8) & 0xFF));
            buffer.push_back(static_cast<char>((weight >> 0) & 0xFF)); //Записали вес файла
            int ioctl = send(sockfd, buffer.data(), (size_t)(header_size), MSG_MORE);

            #ifdef DEBUG_BUILD
            cout << "NEWFILE SENT: " << relpath << endl;
            #endif // DEBUG_BUILD
        }
        catch(std::invalid_argument & ex) //Попалось что то, что не сконвертировать, либо симлинк как то затесался
        {
            cerr << "Got error: " << ex.what() << endl;
            xmliter = xmliter->NextSiblingElement("newfile");
            continue;
        }

        #ifndef DEBUG_BUILD
        //Отправляем файл целиком
        off_t offset = 0;
        while (offset < weight)
        {
            ssize_t sent = sendfile(sockfd, fd, &offset, weight - offset);
            if (sent <= 0)
            {
                close(fd);
                throw std::runtime_error("Ошибка в sendfile при передаче: " + string(relpath));
            }
        }
        #endif // DEBUG_BUILD

        if(close(fd) == -1) //И не забываем закрыть файл
                throw std::runtime_error("File closing error: " + fullname);

        xmliter = xmliter->NextSiblingElement("newfile");
    } //Цикл отправки файлов

    xmliter = xmlel->FirstChildElement("delfile");
    while(xmliter) //Цикл отправки файлов на удаление
    {
        string tag("DELFILE:");
        string relpath(xmliter->Attribute("path"));

        try {
            string header = tag + relpath;
            int ioctl = sendheader(sockfd, header, buffer);

            #ifdef DEBUG_BUILD
            cout << "DELFILE SENT: " << relpath << endl;
            #endif // DEBUG_BUILD
        }
        catch(std::invalid_argument & ex) //Попалось что то, что не сконвертировать, либо симлинк как то затесался
        {
            cerr << "Got error: " << ex.what() << endl;
            xmliter = xmliter->NextSiblingElement("newfile");
            continue;
        }

        xmliter = xmliter->NextSiblingElement("delfile");
    }  //Цикл отправки файлов на удаление

    xmliter = xmlel->FirstChildElement("deldir");
    while(xmliter) // Цикл удаления каталогов
    {
        string tag("DELDIR:");
        string relpath(xmliter->Attribute("path"));

        try {
            string header = tag + relpath;
            int ioctl = sendheader(sockfd, header, buffer);

            #ifdef DEBUG_BUILD
            cout << "DELDIR SENT: " << relpath << endl;
            #endif // DEBUG_BUILD
        }
        catch(std::invalid_argument & ex) //Попалось что то, что не сконвертировать, либо симлинк как то затесался
        {
            xmliter = xmliter->NextSiblingElement("newfile");
            continue;
        }

        xmliter = xmliter->NextSiblingElement("deldir");
    } // Цикл удаления каталогов

    xmliter = xmlel->FirstChildElement("newdir");
    while(xmliter) //Цикл рекурсивной обработки новых каталогов
    {
        string tag("NEWDIR:");
        string relpath(xmliter->Attribute("path"));

        try {
            string header = tag + string(relpath);
            int ioctl = sendheader(sockfd, header, buffer);

            #ifdef DEBUG_BUILD
            cout << "NEWDIR SENT: " << relpath << endl;
            #endif // DEBUG_BUILD
        }
        catch(std::invalid_argument & ex) //Попалось что то, что не сконвертировать, либо симлинк как то затесался
        {
            cerr << "Got error: " << ex.what() << endl;
            xmliter = xmliter->NextSiblingElement("newfile");
            continue;
        }

        send_delta(sockfd, xmliter, filedir, buffer); //Recurion
        xmliter = xmliter->NextSiblingElement("newdir");
    }  //Цикл рекурсивной обработки новых каталогов

    xmliter = xmlel->FirstChildElement("directory");
    while(xmliter) //Цикл обработки директорий, которые не попадают ни в одну категорию
    {
        send_delta(sockfd, xmliter, filedir, buffer);  //Recurion
        xmliter = xmliter->NextSiblingElement("directory");
    } //Цикл обработки директорий, которые не попадают ни в одну категорию
}

void add_news(XMLElement* update, XMLElement* actual)
{
    // Обработка файлов
    XMLElement* actualfileiter = actual->FirstChildElement("file"); //Создаем новый указатель, можно вызывать рекурсию без опасности повредить указатель из предыдущего вызова
    while(actualfileiter)
    {
        XMLElement* newf = update->InsertNewChildElement("newfile");
        const char* newpath = actualfileiter->Attribute("path");
        const char* weigth = actualfileiter->Attribute("weight");
        newf->SetAttribute("path", newpath);
        newf->SetAttribute("weight", weigth);

        actualfileiter = actualfileiter->NextSiblingElement("file");
    }

    XMLElement* actuadiriter = actual->FirstChildElement("directory");
    while(actuadiriter)
    {
        XMLElement* newd = update->InsertNewChildElement("newdir");
        const char* newpath = actuadiriter->Attribute("path");
        const char* weigth = actuadiriter->Attribute("weight");
        newd->SetAttribute("path", newpath);
        newd->SetAttribute("weight", weigth);

        //Рекурсивынй вызов
        add_news(newd, actuadiriter);

        actuadiriter = actuadiriter->NextSiblingElement("directory");
    }
}
