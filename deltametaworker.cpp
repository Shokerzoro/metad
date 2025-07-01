#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <filesystem>
#include <tinyxml2.h>
#include <signal.h>
#include <exception>
#include <vector>

#include "mainfunc.h"
#include "threaddatacontainer.h"
#include "xmlfunc.h"
#include "meta.h"
#include "tstring.h"

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
    ThreadDataContainer* this_thread = reinterpret_cast<ThreadDataContainer*>(data);
    int sockfd;

    try {
        sockfd = this_thread->startrun(); //Захватываем мьютекс общего элемента
        mute_signals(); //Блокировка всех сигналов
    }
    catch(exception & ex) {
        cerr << ex.what();
        return this_thread->stoprun();
    }

    #ifdef DEBUG_BUILD
    cout << "delta_metad_worker starts routine" << endl;
    #endif // DEBUG_BUILD

    //Цикл чтения заголовков и вызов соотв. функций, если бы это был нормальный сервер
    string header;
    string old_meta_name;
    vector<char> buffer(BUFFSIZE);
    uint16_t header_size;
    size_t ioctl = 0;

    //Читаем хэдер
    try {

        ioctl = recvheader(sockfd, header, buffer);
        old_meta_name = (target.string() + "full-meta-" + header + ".XML");
        if(!std::filesystem::exists(old_meta_name)) //Сначала ищем, существует ли старая версия
             throw std::runtime_error("Wrong version request: " + old_meta_name);

        #ifdef DEBUG_BUILD
        cout << endl << "Got update request: " << header << endl;
        #endif // DEBUG_BUILD
    }
    catch(std::runtime_error & ex)
    {
        cerr << "Thread stopped" << endl;
        cerr << ex.what() << endl;
        return this_thread->stoprun();
    }

    string olddate = header;
    string actualdate;
    string actual_meta_name = get_actual(target, actualdate);

    #ifdef DEBUG_BUILD
    cout << "Old full-meta XML doc: " << old_meta_name << endl;
    cout << "Actual full-meta XML doc: " << actual_meta_name << endl;
    #endif // debug

    if(old_meta_name == actual_meta_name) //обновлений нет NOUPDATE
    {
        ioctl = sendheader(sockfd, "NOUPDATE", buffer);
        return this_thread->stoprun();
    }
    else //есть обновления SOMEUPDATE
    {
        ioctl = sendheader(sockfd, "SOMEUPDATE", buffer);
    }

    //Ждем ответа
    try {
        ioctl = recvheader(sockfd, header, buffer);

        #ifdef DEBUG_BUILD
        cout << endl << "Got header: " << header << endl;
        #endif // DEBUG_BUILD

        //Тут отработка запроса, что нам ответили
        if(header != "GETUPDATE")
            throw std::runtime_error("Update Rejected");
    }
    catch(std::runtime_error & ex)
    {
        cerr << "Thread stopped" << endl;
        cerr << ex.what() << endl;
        return this_thread->stoprun();
    }

    //Основная отработка запроса
    string docname = "delta-meta-" + olddate + "-" + actualdate + ".XML";
    string fulldocname = meta.string() + docname; //Имя дельта-файла
    if(!std::filesystem::exists(fulldocname)) //Если нет готовой, создаем новый документ
    {
        //Создание XML документа и корневого элемента
        XMLDocument new_XML_doc;
        XMLElement* update = new_XML_doc.NewElement("update");
        new_XML_doc.InsertFirstChild(update);

        XMLDocument old_XML_doc;
        XMLDocument actual_XML_doc;
        open_XML_doc(old_XML_doc, old_meta_name.c_str());
        open_XML_doc(actual_XML_doc, actual_meta_name.c_str());

        //Открываем root элементы и добавляем аттрибут filedir
        XMLElement* oldupdate = old_XML_doc.RootElement();
        XMLElement* actualupdate = actual_XML_doc.RootElement();
        string actualfiledir = actualupdate->Attribute("filedir");
        update->SetAttribute("date", actualdate.c_str());
        update->SetAttribute("filedir", actualfiledir.c_str());
        //Вызываем рекурсию
        delta_dmeta(oldupdate, actualupdate, update);

        //Сохранение
        new_XML_doc.SaveFile(fulldocname.c_str());

    } //Создаем новый delta

    //Теперь открываем XML файл, парсим его и отправляем данные!!!!
    XMLDocument delta_XML_doc;
    open_XML_doc(delta_XML_doc, fulldocname.c_str());
    XMLElement* update = delta_XML_doc.RootElement();
    string filedir = update->Attribute("filedir");
    if(!std::filesystem::exists(filedir))
    { cerr << "Filepath error: " << filedir << endl; return this_thread->stoprun(); }

    try
    {
        send_delta(sockfd, update, filedir, buffer);
        return this_thread->stoprun();
    }
    catch ( exception & ex)
    {
        cerr << ex.what() << endl;
        string answer = "SERVERERROR";
        header_size = fill_buff(answer, buffer);
        ioctl = writesocket(sockfd, buffer, (size_t)header_size);
    }

    #ifdef DEBUG_BUILD
    cout << "delta_metad_worker ends routine" << endl;
    #endif // DEBUG_BUILD

    return this_thread->stoprun();
}
