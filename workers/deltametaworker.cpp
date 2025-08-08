#include <iostream>
#include <sstream>
#include <filesystem>
#include <tinyxml2.h>
#include <exception>
#include <vector>
#include <pthread.h>

#include "../mainfunc.h"
#include "../threaddatacontainer.h"
#include "../xmlfunc.h"
#include "../meta.h"
#include "../net/netfunc.h"
#include "../exeptions/unaccaptable.h"
#include "../net/tagstrrings.h"

#include <thread>

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

enum class State { NONE, PROPER, NOUPDATE, APPROVED, AGREED, REJECTED, COMPLETE};

// Хэндлер начального состояния (ожидается PROTOCOL = UNETMES)
static void handle_none_state(State& state, netfuncs::ioworker& unetmes_connector);

// Хэндлер состояния PROPER (проверка версии, решение — есть ли обновление)
static void handle_proper_state(State& state, netfuncs::ioworker& unetmes_connector,
                                std::filesystem::path& old_meta_path, std::string& old_version,
                                std::filesystem::path& actual_meta_path, std::string& actual_version);

// Хэндлер состояния NOUPDATE (ожидается PROTOCOL = COMPLETE)
static void handle_noupdate_state(netfuncs::ioworker& unetmes_connector, State& state);

// Хэндлер состояния APPROVED (ожидается AGREE/REJECT)
static void handle_approved_state(State& state, const std::string& actual_version, netfuncs::ioworker& unetmes_connector);

// Функция генерации дельта-файла
static void create_delta_file(const std::filesystem::path& old_meta_path,
                              const std::filesystem::path& actual_meta_path,
                              const std::string& actual_version,
                              const std::string& old_version,
                              std::filesystem::path& deltameta);


void* delta_metad_worker(void* data) //Здесь непосредственно идет обслуживание клиентов
{
    ThreadDataContainer* this_thread = reinterpret_cast<ThreadDataContainer*>(data);
    int sockfd;

    try {
        sockfd = this_thread->startrun(); //Захватываем мьютекс общего элемента
        mute_signals(); //Блокировка всех сигналов
    }
    catch(exception& ex) {
        cerr << ex.what();
        return this_thread->stoprun();
    }

    pthread_t threadid = pthread_self();
    netfuncs::ioworker unetmes_connector(sockfd);
    Path old_meta_path, actual_meta_path, deltametapath, filedir;
    string old_version, actual_version, docname;
    State state = State::NONE;
    int err_counter = 0;

    #ifdef DEBUG_BUILD
    cout << "DeltaWorker " << threadid << " starts routine" << endl;
    unetmes_connector.set_deb_info(true);
    #endif

    while (state != State::AGREED)
    {
        try {
            unetmes_connector.read();

            switch(state)
            {
                case State::NONE:
                    handle_none_state(state, unetmes_connector);
                    break;
                case State::PROPER:
                    handle_proper_state(state, unetmes_connector,old_meta_path, old_version, actual_meta_path, actual_version);
                    #ifdef DEBUG_BUILD
                    cout << "DeltaWorker " << threadid << " : найдена актуальная версия: " << actual_version << endl;
                    cout << "DeltaWorker " << threadid << " : запрошена версия клиента: " << old_version << endl;
                    cout << "DeltaWorker " << threadid << " : delta-meta XML документ: " << deltametapath << endl;
                    #endif
                    break;
                case State::NOUPDATE:
                    handle_noupdate_state(unetmes_connector, state);
                    break;
                case State::APPROVED:
                    handle_approved_state(state, actual_version, unetmes_connector);
                    break;
                default:
                    return this_thread->stoprun();
            }

        }
        catch (Unacceptable& ex)
        {
            std::cerr << "Got unaccaptable exeption" << std::endl;
            std::cerr << ex.what() << std::endl;
            return this_thread->stoprun();
        }
        catch (std::runtime_error& ex)
        {
            cerr << "Runtime error: " << ex.what() << endl;
            return this_thread->stoprun();
        }
        catch (std::invalid_argument& ex)
        {
            if (err_counter++ >= 3)
            {
                std::cerr << "Client error: protocol exeptions limitated" << std::endl;
                return this_thread->stoprun();
            }
            continue;
        }
    }

    if (state == State::AGREED) //На всякий случай с проверкой
    {
        //Получаем имя дельтафайла
        docname = "delta-meta-" + old_version + "-" + actual_version + ".XML";
        deltametapath = meta / docname;

        //Генерируем дельтафайл, если нет
        if (!std::filesystem::exists(deltametapath))
            create_delta_file(old_meta_path, actual_meta_path, actual_version, old_version, deltametapath);

        //Открываем для отправки
        XMLDocument delta_XML_doc;
        open_XML_doc(delta_XML_doc, deltametapath.string());
        XMLElement* update = delta_XML_doc.RootElement();
        filedir = update->Attribute("filedir");
        if (!std::filesystem::exists(filedir))
            throw std::runtime_error("Filedir does not exist");

        try {
            std::cout << "DeltaWorker " << threadid << ": started update sending: " << old_version << "-" << actual_version  << std::endl;
            //Основная функция отправки
            send_delta(update, filedir, unetmes_connector);
            unetmes_connector.send(TagStrings::PROTOCOL, TagStrings::COMPLETE);
        }
        catch (std::runtime_error& ex)
        {
            cerr << "DeltaWorker : ошибка при отправке обновлений: " << ex.what() << endl;
            unetmes_connector.send(TagStrings::PROTOCOL, TagStrings::SERVERERROR);
            return this_thread->stoprun();
        }

    }

    return this_thread->stoprun();
}

// ---------- Хэндлеры ----------

static void handle_none_state(State& state, netfuncs::ioworker & unetmes_connector)
{
    if (unetmes_connector.fullcmp(TagStrings::PROTOCOL, TagStrings::UNETMES))
    {
        state = State::PROPER;
        unetmes_connector.send(TagStrings::PROTOCOL, TagStrings::UNETMES);
    }
    else
        throw Unacceptable("Wrong protocol");
}

static void handle_proper_state(State& state, netfuncs::ioworker & unetmes_connector,
                                Path& old_meta_path, string& old_version,
                                Path& actual_meta_path, string& actual_version)
{


    if (!unetmes_connector.tagcmp(TagStrings::VERSION))
        throw Unacceptable("Wrong version tag");

    old_meta_path = target / ("full-meta-" + unetmes_connector.get_value() + ".XML");

    if (std::filesystem::exists(old_meta_path))
    {
        old_version = unetmes_connector.get_value() ;
        get_actual(target, actual_meta_path);
        get_version(actual_meta_path, actual_version);

        if (old_meta_path == actual_meta_path)
        {
            state = State::NOUPDATE;
            unetmes_connector.send(TagStrings::PROTOCOL, TagStrings::NOUPDATE);
            return;
        }
        else
        {
            state = State::APPROVED;
            unetmes_connector.send(TagStrings::PROTOCOL, TagStrings::SOMEUPDATE);
            return;
        }
    }
    else
        throw Unacceptable("Wrong version request");
}

static void handle_noupdate_state(netfuncs::ioworker & unetmes_connector, State& state)
{
    if (unetmes_connector.fullcmp(TagStrings::PROTOCOL, TagStrings::COMPLETE))
    {
        state = State::COMPLETE;
        return;
    }
    throw std::invalid_argument("Wrong noupdate answer");
}

static void handle_approved_state(State& state, const string& actual_version, netfuncs::ioworker & unetmes_connector)
{

    if (unetmes_connector.fullcmp(TagStrings::PROTOCOL, TagStrings::AGREE))
    {
        state = State::AGREED;
        unetmes_connector.send(TagStrings::VERSION, actual_version.c_str());
        return;
    }
    if (unetmes_connector.fullcmp(TagStrings::PROTOCOL, TagStrings::REJECT))
    {
        state = State::REJECTED;
        return;
    }
}

static void create_delta_file(const Path& old_meta_path, const Path& actual_meta_path,
                              const string& actual_version, const string& old_version, Path& deltameta)
{
    #ifdef DEBUG_BUILD
    pthread_t threadid = pthread_self();
    cout << "DeltaWorker " << threadid << " : начинаю генерацию дельта-файла между версиями "
         << old_version << " и " << actual_version << endl;
    #endif

    XMLDocument new_XML_doc;
    XMLElement* update = new_XML_doc.NewElement("update");
    new_XML_doc.InsertFirstChild(update);

    XMLDocument old_XML_doc, actual_XML_doc;
    open_XML_doc(old_XML_doc, old_meta_path.string().c_str());
    open_XML_doc(actual_XML_doc, actual_meta_path.string().c_str());

    XMLElement* oldupdate = old_XML_doc.RootElement();
    XMLElement* actualupdate = actual_XML_doc.RootElement();

    string actualfiledir = actualupdate->Attribute("filedir");
    update->SetAttribute("version", actual_version.c_str());
    update->SetAttribute("filedir", actualfiledir.c_str());

    //Вызов функции генерации
    delta_dmeta(oldupdate, actualupdate, update);

    new_XML_doc.SaveFile(deltameta.string().c_str());

    #ifdef DEBUG_BUILD
    cout << "DeltaWorker " << threadid << " : дельта-файл успешно создан: " << deltameta << endl;
    #endif
}

