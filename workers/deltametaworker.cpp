#include <iostream>
#include <sstream>
#include <filesystem>
#include <tinyxml2.h>
#include <exception>
#include <vector>

#include "../mainfunc.h"
#include "../threaddatacontainer.h"
#include "../xmlfunc.h"
#include "../meta.h"
#include "../net/netfunc.h"
#include "../exeptions/unaccaptable.h"

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
    }
    catch(exception & ex) {
        cerr << ex.what();
        return this_thread->stoprun();
    }

    string header, tag, value;
    vector<char> buffer(BUFFSIZE); //Отдельный буфер на каждый поток
    Path old_meta_path, actual_meta_path;
    string old_version, actual_version;
    enum class State { NONE, PROPER, APPROVED, AGREED };
    State state = State::NONE;
    int err_counter = 0;
    size_t bytes_readed = 0; //Для возможности более тонкого контроля

    #ifdef DEBUG_BUILD
    cout << "delta_metad_worker starts routine" << endl;
    #endif // DEBUG_BUILD

    while (true) //Цикл чтения запросов
    { try {
        //Получаем версию
        bytes_readed = recvheader(sockfd, header, buffer);
        if(bytes_readed < 0)
            throw std::runtime_error("Connection lost");
        parse_header(header, tag, value);

        if(state == State::NONE && tag == TagStrings::PROTOCOL) //Первый запрос, идентификация протокола
        {
            //Uniter Network for Manufacturing Execution Systems
            if( value == TagStrings::UNETMES)
            {
                state = State::PROPER;
                header = build_header(TagStrings::PROTOCOL, TagStrings::UNETMES);
                sendheader(sockfd, header, buffer);
            }
            else
                return this_thread->stoprun();
        } //Первый запрос, идентификация протокола
        if(state == State::PROPER && tag == TagStrings::VERSION) //Получаем версию
        {
            old_meta_path = target / ("full-meta-" + value + ".XML");
            
            if(std::filesystem::exists(old_meta_path)) //Если существует
            {
                state = State::APPROVED;
                old_version = old_meta_path.string();
                get_actual(target, actual_meta_path, actual_version);

                if(old_meta_path == actual_meta_path) //обновлений нет
                {
                    header = build_header(TagStrings::PROTOCOL, TagStrings::NOUPDATE);
                    bytes_readed = sendheader(sockfd, header, buffer);
                    return this_thread->stoprun();
                }
                else
                {
                    header = build_header(TagStrings::PROTOCOL, TagStrings::SOMEUPDATE);
                    bytes_readed = sendheader(sockfd, header, buffer);
                }
            }
            else
                throw Unacceptable("Wrong version request");
        } //Получаем версию
        if(state == State::APPROVED && tag == TagStrings::PROTOCOL) //Получаем подтверждение обновлений
        {
            if(value == "AGREE") //Получаем согласие
            {
                state = State::AGREED;
                header = build_header(TagStrings::VERSION, actual_version.c_str());
                bytes_readed = sendheader(sockfd, header, buffer);
                break;
            }
            if(header == "REJECT") //Получаем отказ
            {
                #ifdef DEBUG_BUILD
                std::cout << "Client rejected getting update" << std::endl;
                #endif
                return this_thread->stoprun();
            } //Получаем подтверждение обновлений
        } //Цикл чтения запросов
    } //Try
    catch (Unacceptable & ex)
    {
        std::cerr << "Got unaccaptable exeption" << std::endl;
        std::cerr << ex.what() << std::endl;
        return this_thread->stoprun();
    }
    catch(std::runtime_error & ex)
    {
        cerr << "Runtime error: " << ex.what() << endl;
        return this_thread->stoprun();
    }
    catch(std::invalid_argument & ex)
    {
        if(err_counter++ >= 3)
        {
            std::cerr << "Client error: protocol exeptions limitated" << std::endl;
            return this_thread->stoprun();
        }
        continue;
    } } //Обработка исключений цикла чтения запросов

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
        bytes_readed = sendheader(sockfd, actual_version, buffer);
        send_delta(sockfd, update, filedir, buffer); //Основная операция отправки данных
    }
    catch ( std::runtime_error & ex)
    {
        cerr << "Got runtime_error while sending update: " << ex.what() << endl;
        bytes_readed = sendheader(sockfd, "SERVERERROR", buffer);
        return this_thread->stoprun();
    }

    #ifdef DEBUG_BUILD
    cout << "delta_metad_worker ends routine" << endl;
    #endif // DEBUG_BUILD

    bytes_readed = sendheader(sockfd, "COMPLETE", buffer); //Отправляем напоследок последнюю версию
    return this_thread->stoprun();
}
