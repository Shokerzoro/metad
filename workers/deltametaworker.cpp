#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <errno.h>
#include <string.h>
#include <filesystem>
#include <tinyxml2.h>
#include <signal.h>
#include <exception>
#include <vector>

#include "../mainfunc.h"
#include "../threaddatacontainer.h"
#include "../xmlfunc.h"
#include "../meta.h"
#include "../tstring.h"

using namespace tinyxml2;
using std::cout;
using std::cerr;
using std::endl;
using std::stringstream;
using std::string;
using std::exception;
using std::vector;
using Path = std::filesystem::path;

extern Path target;
extern Path meta;

void* delta_metad_worker(void* data) //Здесь непосредственно идет обслуживание клиентов
{
    //Получаем указатель на управляющую структуру
    ThreadDataContainer* this_thread = reinterpret_cast<ThreadDataContainer*>(data);
    int sockfd;

    //Получаем дескриптор сокета блокируя общую структуру данных и блокируем сигналы
    try {
        sockfd = this_thread->startrun(); //Захватываем мьютекс общего элемента
        mute_signals(); //Блокировка всех сигналов
        //Чтение ключевого слова из сокета для исключения лишних запросов, check_protocol что то такое
    }
    catch(exception & ex) {
        cerr << ex.what();
        return this_thread->stoprun();
    }

    string header;
    vector<char> buffer(BUFFSIZE); //Отдельный буфер на каждый поток
    Path old_meta_path, actual_meta_path;
    string old_version, actual_version;
    enum class State { NONE, PROPER, REQUESTED, APPROVED, AGREED };
    State state = State::NONE;
    int err_counter = 0;
    size_t ioctl = 0; //Для возможности более тонкого контроля

    #ifdef DEBUG_BUILD
    cout << "delta_metad_worker starts routine" << endl;
    #endif // DEBUG_BUILD

    while (true) //Цикл чтения запросов
    { try {
        ioctl = recvheader(sockfd, header, buffer);
        if(ioctl < 0)
            throw std::runtime_error("Connection lost");

        if(state == State::NONE) //Первый запрос
        {
            if(header == "UNET-MES") //Uniter Network for Manufacturing Execution Systems
                state = State::PROPER;
            else
                return this_thread->stoprun();
        }
        if(state == State::PROPER) //Первый запрос
        {
            if(header == "GETUPDATE")
                state = State::REQUESTED;
            else
                throw std::invalid_argument("Wrong request");
        }
        if(state == State::REQUESTED) //Получаем старую
        {
            old_meta_path = target / ("full-meta-" + header + ".XML");
            if(std::filesystem::exists(old_meta_path))
            {
                state = State::APPROVED;
                old_version = header;
                get_actual(target, actual_meta_path, actual_version);

                if(old_meta_path == actual_meta_path) //обновлений нет
                {
                    ioctl = sendheader(sockfd, "NOUPDATE", buffer);
                    return this_thread->stoprun();
                }
                else
                {
                    ioctl = sendheader(sockfd, "SOMEUPDATE", buffer);
                }
            }
            else
                throw std::invalid_argument("Wrong version request: " + old_meta_path.string());
        }
        if(state == State::APPROVED)
        {
            if(header == "AGREE") //Получаем согласие
            {
                state = State::AGREED;
                break;
            }
            if(header == "REJECT") //Не хотят
                return this_thread->stoprun();

            throw std::invalid_argument("Wrong answer");
        }
    }
    catch(std::invalid_argument & ex)
    {
        if(err_counter++ >= 3)
        {
            cerr << "Client error: " << ex.what() << endl;
            sendheader(sockfd, "GOODBYE", buffer);
            return this_thread->stoprun();
        }
        continue;
    }
    catch(std::runtime_error & ex)
    {
        cerr << "Runtime error: " << ex.what() << endl;
        return this_thread->stoprun();
    } } //Цикл чтения запросов

    #ifdef DEBUG_BUILD
    cout << "Old full-meta XML doc: " << old_meta_path << endl;
    cout << "Actual full-meta XML doc: " << actual_meta_path << endl;
    #endif // debug

    //Формируем название deltameta файла
    string docname = "delta-meta-" + old_version + "-" + actual_version + ".XML";
    Path deltameta(meta.string() + docname); //Путь к файлу дельты

    if(!std::filesystem::exists(deltameta)) //Если нет готовой, создаем новый документ
    {
        //Создание XML документа и корневого элемента
        XMLDocument new_XML_doc;
        XMLElement* update = new_XML_doc.NewElement("update");
        new_XML_doc.InsertFirstChild(update);

        XMLDocument old_XML_doc;
        XMLDocument actual_XML_doc;
        open_XML_doc(old_XML_doc, old_meta_path.string().c_str());
        open_XML_doc(actual_XML_doc, actual_meta_path.string().c_str());

        //Открываем root элементы и добавляем аттрибут filedir
        XMLElement* oldupdate = old_XML_doc.RootElement();
        XMLElement* actualupdate = actual_XML_doc.RootElement();
        //Для того, чтобы знать, где находятся файлы. Пути в fullXML относительные, но есть ссылка на каталог
        string actualfiledir = actualupdate->Attribute("filedir");
        update->SetAttribute("version", actual_version.c_str());
        update->SetAttribute("filedir", actualfiledir.c_str());
        //Вызываем рекурсию
        delta_dmeta(oldupdate, actualupdate, update);

        //Сохранение
        new_XML_doc.SaveFile(deltameta.string().c_str());

    } //Создаем новый delta

    //Теперь открываем XML файл, парсим его и отправляем данные!!!!
    XMLDocument delta_XML_doc;
    open_XML_doc(delta_XML_doc, deltameta.string().c_str());

    //Получаем корневой элемент и каталог хранения файлов
    XMLElement* update = delta_XML_doc.RootElement();
    string filedir = update->Attribute("filedir");
    if(!std::filesystem::exists(filedir))
    { cerr << "Filepath error: " << filedir << endl; return this_thread->stoprun(); }

    try { //Основная операция по отправке новых файлов
        ioctl = sendheader(sockfd, actual_version, buffer);
        send_delta(sockfd, update, filedir, buffer); //Основная операция отправки данных
    }
    catch ( std::runtime_error & ex)
    {
        cerr << "Got runtime_error while sending update: " << ex.what() << endl;
        ioctl = sendheader(sockfd, "SERVERERROR", buffer);
        return this_thread->stoprun();
    }

    #ifdef DEBUG_BUILD
    cout << "delta_metad_worker ends routine" << endl;
    #endif // DEBUG_BUILD

    ioctl = sendheader(sockfd, "COMPLETE", buffer); //Отправляем напоследок последнюю версию
    return this_thread->stoprun();
}
