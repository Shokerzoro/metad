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
#include "net/netfunc.h"
#include "tstring.h"

using namespace tinyxml2;
using Direntry = std::filesystem::directory_entry;
using Diriter = std::filesystem::directory_iterator;
using Path = std::filesystem::path;
using std::exception;

//Функция, которая рекурсивно добавляет записи о новых файлах и каталогах
//Вызывается из delta_dmeta, когда завершена отработка наименьшего множества каталогов
//И остались новые записи каталогов (новые каталоги и файлы внутри)
static void add_news(XMLElement* update, XMLElement* actual);

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
            std::cout << newdirentry.path() << ":    directory" << std::endl;
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
            std::cout << newdirentry.path() << ":   " << "file" << std::endl;
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
        std::cout << "Error: " << ex.what() << std::endl;
        return;
    }
}

//Принимает указатеи на элементы, которые являются "каталогами"
void delta_dmeta(XMLElement* oldv, XMLElement* actualv, XMLElement* update)
{
    //!!! - - - - - - начало отработки файлов
    // Инициализация итераторов
    XMLElement* oldviter = oldv->FirstChildElement("file"); //Элемент из старого XML документа
    XMLElement* actualviter = actualv->FirstChildElement("file"); //Элемент из актуального
    while(oldviter && actualviter) //Отработка в интервале наименьшего множества записей (только файлы)
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
                const char* hash = actualviter->Attribute("sha256");
                newfile->SetAttribute("path", newpath);
                newfile->SetAttribute("weight", weigth);
                newfile->SetAttribute("sha256", hash);
            }
            // Переходим к следующим элементам
            oldviter = oldviter->NextSiblingElement("file");
            actualviter = actualviter->NextSiblingElement("file");
        }
        // Случай несовпадения путей (т.е. либо новая запись actualiter либо отсутсвует старая)
        else {
            //Проверка следующим способом: создаем временный итератор curriter, которым проходим актуальную версию
            //Либо до, конца либо до совпадения путей
            XMLElement* curriter = actualviter;
            while(curriter && !cmp_XML_path(oldviter, curriter))
            {
                curriter = curriter->NextSiblingElement("file");
            }

            // Если файл не найден в актуальной версии, значит отсутсвует старая запись (файл удален)
            // Формируем запись удаления и итерируем oldviter на следующую позицию
            if(!curriter)
            {
                // Создаем элемент удаления
                XMLElement* delfile = update->InsertNewChildElement("delfile");
                const char* delpath = oldviter->Attribute("path");
                delfile->SetAttribute("path", delpath);
                oldviter = oldviter->NextSiblingElement("file");
            }
            // Если файл найден, то в интервале между actualviter и временным итератором curriter есть новые записи
            // Мы проходим этот интервал и добавляем записи о новом файле
            else
            {
                // Отработка добавленные файлы
                XMLElement* temp = actualviter;
                while(temp != curriter)
                {
                    XMLElement* newfile = update->InsertNewChildElement("newfile");
                    const char* newpath = temp->Attribute("path");
                    const char* weigth = temp->Attribute("weight");
                    const char* hash = temp->Attribute("sha256");
                    newfile->SetAttribute("path", newpath);
                    newfile->SetAttribute("weight", weigth);
                    newfile->SetAttribute("sha256", hash);

                    temp = temp->NextSiblingElement("file");
                }
                // Возвращаемся к обработке текущего файла
                actualviter = curriter->NextSiblingElement("file");
            }
        }
    } //Отработка в интервале наименьшего множества записей (только файлы)

    //Когда одно из множеств (либо старые, либо новые записи) закончилось, остаются следующие варианты
    //Либо есть необработаные старые записи (значит они удалены), либо новые записи (значит новые файлы)
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
        const char* hash = actualviter->Attribute("sha256");
        newfile->SetAttribute("path", newpath);
        newfile->SetAttribute("weight", weigth);
        newfile->SetAttribute("sha256", hash);
        actualviter = actualviter->NextSiblingElement("file");
    }
    //!!! - - - - - - конец отработки файлов

    //!!! - - - - - - начало отработки каталогов
    XMLElement* olddir = oldv->FirstChildElement("directory");
    XMLElement* newdir = actualv->FirstChildElement("directory");
    while (olddir && newdir) //Отработка наименьшего множества записей
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
    } //Отработка наименьшего множества записей

    //Аналогичная ситуация после отработки наименьшего множества записей
    // Удалённые каталоги
    while (olddir)
    {
        XMLElement* deldir = update->InsertNewChildElement("deldir");
        deldir->SetAttribute("path", olddir->Attribute("path"));
        olddir = olddir->NextSiblingElement("directory");
    }

    // Добавленные новых каталогов и новых файлов внутри них
    while (newdir) //Элемент, который мы парсим
    {
        XMLElement* newd = update->InsertNewChildElement("newdir"); //Новый элемент в update
        newd->SetAttribute("path", newdir->Attribute("path"));

        // Добавленные новых каталогов и новых файлов внутри них
        add_news(newd, newdir);

        newdir = newdir->NextSiblingElement("directory");
    }
    //!!! - - - - - - конец отработки каталогов
}

//Отправка данных, при парсинге файла delta-meta
void send_delta(int sockfd, XMLElement* xmlel, const string & filedir, std::vector<char> & buffer)
{
    XMLElement* xmliter = nullptr;
    uint16_t header_size;
    size_t bytes_readed;
    std::string header, tag, value, relpath, fullname;

    //Цикл отправки файлов
    for(xmliter = xmlel->FirstChildElement("newfile"); xmliter; xmliter = xmliter->NextSiblingElement("newfile"))
    {
        std::string weightstr, file_hash;
        int fd; //Дескриптор файла для sendfile

        try {
            relpath = xmliter->Attribute("path");
            fullname = filedir + relpath;
            if (!std::filesystem::exists(fullname))
                throw std::runtime_error("Файл не найден: " + fullname);

            char* endptr = nullptr;
            weightstr = xmliter->Attribute("weight");
            file_hash = xmliter->Attribute("sha256");
            uint32_t weight = std::strtoll(weightstr.c_str(), &endptr, 10);
            if(weight == 0)
                throw std::invalid_argument("File weight == 0. Symlink maybe. How can it get there?");
            if (file_hash.empty())
                throw std::invalid_argument("No file hash. Check deltameta generator logic.");

            if((fd = open(fullname.c_str(), O_RDONLY)) == -1)
                throw std::runtime_error("Ошибка открытия файла: " + fullname);

            //Отправляем заголовок с путем
            header = build_header(TagStrings::NEWFILE, relpath.c_str());
            bytes_readed = sendheader(sockfd, header, buffer);
#ifdef DEBUG_BUILD
            pthread_t threadid = pthread_self();
            std::cout << "DeltaWorker " << threadid << "sending header: " << header << std::endl;
#endif

            //Отправим заголовок с хэшем
            header = build_header(TagStrings::HASH, file_hash.c_str());
            bytes_readed = sendheader(sockfd, header, buffer);
#ifdef DEBUG_BUILD
            std::cout << "DeltaWorker " << threadid << "sending header: " << header << std::endl;
#endif

            bytes_readed = recvheader(sockfd, header, buffer);
            parse_header(header, tag, value);
#ifdef DEBUG_BUILD
            std::cout << "DeltaWorker " << threadid << "got header: " << header << std::endl;
#endif

            if (tag == TagStrings::NEWFILE && value == TagStrings::AGREE) //Отправляем файл
            {
                bytes_readed = send_file(sockfd, fd, weight, buffer);
                if(bytes_readed != weight)
                {
                    close(fd);
                    throw std::runtime_error("Error while sending file" + fullname);
                }

                //И не забываем закрыть файл если все прошло успешно
                if(close(fd) == -1)
                    throw std::runtime_error("File closing error: " + fullname);

                #ifdef DEBUG_BUILD
                std::cout << "Newfile sent: " << relpath << std::endl;
                #endif // DEBUG_BUILD
            }
            else if (tag == TagStrings::NEWFILE && value == TagStrings::REJECT)
            {
                #ifdef DEBUG_BUILD
                std::cout << "File rejected: " << relpath << std::endl;
                #endif
            }
            else
                throw std::invalid_argument("Unpropriate newfile answer");
        } //Блок try цикла отправки файлов
        catch(std::invalid_argument & ex) //Не критичное исключение, а выбрасывают в отказ на одной процедуре ниже
        {
            std::cerr << "Got error: " << ex.what() << std::endl;
            xmliter = xmliter->NextSiblingElement("newfile");
            continue;
        } } //Цикл отправки файлов

    //Цикл отправки файлов на удаление
    for(xmliter = xmlel->FirstChildElement("delfile"); xmliter; xmliter = xmliter->NextSiblingElement("delfile"))
    {
        relpath = xmliter->Attribute("path");

        try {
            header = build_header(TagStrings::DELFILE, relpath.c_str());
            bytes_readed = sendheader(sockfd, header, buffer);

            #ifdef DEBUG_BUILD
            std::cout << "DELFILE SENT: " << relpath << std::endl;
            #endif // DEBUG_BUILD
        }
        catch(std::invalid_argument & ex) //Попалось что то, что не сконвертировать, либо симлинк как то затесался
        {
            std::cerr << "Got error: " << ex.what() << std::endl;
            continue;
        }
    }  //Цикл отправки файлов на удаление

    // Цикл удаления каталогов
    for(xmliter = xmlel->FirstChildElement("deldir"); xmliter; xmliter = xmliter->NextSiblingElement("deldir"))
    {
        relpath = xmliter->Attribute("path");

        try {
            header = build_header(TagStrings::DELDIR, relpath.c_str());
            bytes_readed = sendheader(sockfd, header, buffer);

            #ifdef DEBUG_BUILD
            std::cout << "DELDIR SENT: " << relpath << std::endl;
            #endif // DEBUG_BUILD
        }
        catch(std::invalid_argument & ex) //Попалось что то, что не сконвертировать, либо симлинк как то затесался
        {
            std::cout << "Invalid arg while deldir proccecing: " << ex.what() << std::endl;
            continue;
        }
    } // Цикл удаления каталогов

    //Цикл рекурсивной обработки новых каталогов
    for(xmliter = xmlel->FirstChildElement("newdir"); xmliter; xmliter = xmliter->NextSiblingElement("newdir"))
    {
        relpath = xmliter->Attribute("path");

        try {
            header = tag + string(relpath);
            bytes_readed = sendheader(sockfd, header, buffer);

            #ifdef DEBUG_BUILD
            std::cout << "NEWDIR SENT: " << relpath << std::endl;
            #endif // DEBUG_BUILD
        }
        catch(std::invalid_argument & ex) //Попалось что то, что не сконвертировать, либо симлинк как то затесался
        {
            std::cerr << "Got error: " << ex.what() << std::endl;
            continue;
        }

        send_delta(sockfd, xmliter, filedir, buffer); //Recurion
    }  //Цикл рекурсивной обработки новых каталогов

    //Цикл обработки директорий, которые не попадают ни в одну категорию
    for(xmliter = xmlel->FirstChildElement("directory"); xmliter; xmliter = xmliter->NextSiblingElement("directory"))
    {
        send_delta(sockfd, xmliter, filedir, buffer);  //Recurion
    } //Цикл обработки директорий, которые не попадают ни в одну категорию
}

//Функция, которая рекурсивно добавляет записи о новых файлах и каталогах
//Вызывается из delta_dmeta, когда завершена отработка наименьшего множества каталогов
//И остались новые записи каталогов (новые каталоги и файлы внутри)
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
