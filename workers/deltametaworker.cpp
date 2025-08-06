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

enum class State { NONE, PROPER, NOUPDATE, APPROVED, AGREED };

// Объявления хэндлеров
static bool handle_none_state(int sockfd, const string& tag, const string& value, State& state, vector<char>& buffer);
static bool handle_proper_state(int sockfd, const string& tag, const string& value, State& state,
                                vector<char>& buffer, Path& old_meta_path, string& old_version,
                                Path& actual_meta_path, string& actual_version);
static bool handle_noupdate_state(int sockfd, const string& tag, const string& value, ThreadDataContainer* thread_data);
static bool handle_approved_state(int sockfd, const string& tag, const string& value, State& state,
                                  const string& actual_version, vector<char>& buffer, ThreadDataContainer* thread_data);
static bool handle_agreed_state(int sockfd, const Path& old_meta_path, const Path& actual_meta_path,
                                const string& old_version, const string& actual_version,
                                vector<char>& buffer, ThreadDataContainer* thread_data);
static void create_delta_file(const Path& old_meta_path, const Path& actual_meta_path,
                              const string& actual_version, const string& old_version, Path& deltameta);
static bool send_delta_file(int sockfd, const Path& deltameta, vector<char>& buffer,
                            const string& actual_version, ThreadDataContainer* thread_data);

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

    string header, tag, value;
    vector<char> buffer(BUFFSIZE);
    Path old_meta_path, actual_meta_path;
    string old_version, actual_version;
    State state = State::NONE;
    int err_counter = 0;
    size_t bytes_readed = 0;

    #ifdef DEBUG_BUILD
    pthread_t threadid = pthread_self();
    cout << "DeltaWorker " << threadid << " starts routine" << endl;
    #endif

    while (true)
    {
        try {
            bytes_readed = recvheader(sockfd, header, buffer);
            if (bytes_readed < 0)
                throw std::runtime_error("Connection lost");
            parse_header(header, tag, value);

            #ifdef DEBUG_BUILD
            cout << "DeltaWorker " << threadid << " got header: " << header << endl;
            #endif

            bool continue_loop = true;
            switch(state)
            {
                case State::NONE:
                    continue_loop = handle_none_state(sockfd, tag, value, state, buffer);
                    break;
                case State::PROPER:
                    continue_loop = handle_proper_state(sockfd, tag, value, state, buffer,
                                                        old_meta_path, old_version, actual_meta_path, actual_version);
                    break;
                case State::NOUPDATE:
                    continue_loop = handle_noupdate_state(sockfd, tag, value, this_thread);
                    break;
                case State::APPROVED:
                    continue_loop = handle_approved_state(sockfd, tag, value, state, actual_version, buffer, this_thread);
                    break;
                case State::AGREED:
                    continue_loop = false;
                    break;
            }

            if (!continue_loop)
                break;
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

    if (state == State::AGREED)
    {
        bool result = handle_agreed_state(sockfd, old_meta_path, actual_meta_path,
                                          old_version, actual_version, buffer, this_thread);
        if (!result) return this_thread->stoprun();
    }

    return this_thread->stoprun();
}

// ---------- Хэндлеры ----------

static bool handle_none_state(int sockfd, const string& tag, const string& value, State& state, vector<char>& buffer)
{
    if (tag == TagStrings::PROTOCOL && value == TagStrings::UNETMES)
    {
        state = State::PROPER;
        string header = build_header(TagStrings::PROTOCOL, TagStrings::UNETMES);
        #ifdef DEBUG_BUILD
        pthread_t threadid = pthread_self();
        std::cout << "DeltaWorker " << threadid << " sending header: " << header << std::endl;
        #endif
        sendheader(sockfd, header, buffer);
        return true;
    }
    return false;
}

static bool handle_proper_state(int sockfd, const string& tag, const string& value, State& state,
                                vector<char>& buffer, Path& old_meta_path, string& old_version,
                                Path& actual_meta_path, string& actual_version)
{
    #ifdef DEBUG_BUILD
    pthread_t threadid = pthread_self();
    #endif

    if (tag != TagStrings::VERSION) return true;

    old_meta_path = target / ("full-meta-" + value + ".XML");

    if (std::filesystem::exists(old_meta_path))
    {
        old_version = value;
        get_actual(target, actual_meta_path);
        get_version(actual_meta_path, actual_version);

        #ifdef DEBUG_BUILD
        cout << "DeltaWorker " << threadid << " : найдена актуальная версия: " << actual_version << endl;
        cout << "DeltaWorker " << threadid << " : запрошена версия клиента: " << old_version << endl;
        #endif

        if (old_meta_path == actual_meta_path)
        {
            state = State::NOUPDATE;
            string header = build_header(TagStrings::PROTOCOL, TagStrings::NOUPDATE);
            #ifdef DEBUG_BUILD
            cout << "DeltaWorker " << threadid << " : версии совпадают, обновлений нет. Отправляю NOUPDATE." << endl;
            #endif
            sendheader(sockfd, header, buffer);
            return false;
        }
        else
        {
            state = State::APPROVED;
            string header = build_header(TagStrings::PROTOCOL, TagStrings::SOMEUPDATE);
            #ifdef DEBUG_BUILD
            cout << "DeltaWorker " << threadid << " : версии различаются, есть обновления. Отправляю SOMEUPDATE." << endl;
            #endif
            sendheader(sockfd, header, buffer);
            return true;
        }
    }
    else
    {
        #ifdef DEBUG_BUILD
        pthread_t threadid = pthread_self();
        cout << "DeltaWorker " << threadid << " : ошибка — неверный запрос версии: " << value << endl;
        #endif
        throw Unacceptable("Wrong version request");
    }
}

static bool handle_noupdate_state(int sockfd, const string& tag, const string& value, ThreadDataContainer* thread_data)
{
    if (tag == TagStrings::PROTOCOL && value == TagStrings::COMPLETE)
        return false;
    return true;
}

static bool handle_approved_state(int sockfd, const string& tag, const string& value, State& state,
                                  const string& actual_version, vector<char>& buffer, ThreadDataContainer* thread_data)
{
    #ifdef DEBUG_BUILD
    pthread_t threadid = pthread_self();
    #endif

    if (tag == TagStrings::PROTOCOL)
    {
        if (value == "AGREE")
        {
            state = State::AGREED;
            string header = build_header(TagStrings::VERSION, actual_version.c_str());
            #ifdef DEBUG_BUILD
            cout << "DeltaWorker " << threadid << " sending header: " << header << endl;
            #endif
            sendheader(sockfd, header, buffer);
            return false;
        }
        if (value == "REJECT")
        {
            #ifdef DEBUG_BUILD
            cout << "DeltaWorker " << threadid << " : клиент отказался от обновления" << endl;
            #endif
            thread_data->stoprun();
            return false;
        }
    }
    return true;
}

static bool handle_agreed_state(int sockfd, const Path& old_meta_path, const Path& actual_meta_path,
                                const string& old_version, const string& actual_version,
                                vector<char>& buffer, ThreadDataContainer* thread_data)
{
    #ifdef DEBUG_BUILD
    pthread_t threadid = pthread_self();
    cout << "DeltaWorker " << threadid << " : старый full-meta XML документ: " << old_meta_path << endl;
    cout << "DeltaWorker " << threadid << " : актуальный full-meta XML документ: " << actual_meta_path << endl;
    #endif

    string docname = "delta-meta-" + old_version + "-" + actual_version + ".XML";
    Path deltameta = meta / docname;

    if (!std::filesystem::exists(deltameta))
        create_delta_file(old_meta_path, actual_meta_path, actual_version, old_version, deltameta);

    return send_delta_file(sockfd, deltameta, buffer, actual_version, thread_data);
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

    delta_dmeta(oldupdate, actualupdate, update);

    new_XML_doc.SaveFile(deltameta.string().c_str());

    #ifdef DEBUG_BUILD
    cout << "DeltaWorker " << threadid << " : дельта-файл успешно создан: " << deltameta << endl;
    #endif
}

static bool send_delta_file(int sockfd, const Path& deltameta, vector<char>& buffer,
                            const string& actual_version, ThreadDataContainer* thread_data)
{
    #ifdef DEBUG_BUILD
    pthread_t threadid = pthread_self();
    cout << "DeltaWorker " << threadid << " : начинаю отправку обновления версии " << actual_version << endl;
    #endif

    XMLDocument delta_XML_doc;
    open_XML_doc(delta_XML_doc, deltameta.string().c_str());

    XMLElement* update = delta_XML_doc.RootElement();
    string filedir = update->Attribute("filedir");
    if (!std::filesystem::exists(filedir))
    {
        cerr << "DeltaWorker : путь для файлов обновления не найден: " << filedir << endl;
        return false;
    }

    try {
        sendheader(sockfd, actual_version, buffer);
        send_delta(sockfd, update, filedir, buffer);
    }
    catch (std::runtime_error& ex)
    {
        cerr << "DeltaWorker : ошибка при отправке обновления: " << ex.what() << endl;
        sendheader(sockfd, "SERVERERROR", buffer);
        return false;
    }

    #ifdef DEBUG_BUILD
    cout << "DeltaWorker " << threadid << " : отправка обновления версии " << actual_version << " завершена" << endl;
    cout << "DeltaWorker " << threadid << " : delta_metad_worker ends routine" << endl;
    #endif

    sendheader(sockfd, "COMPLETE", buffer);

    return true;
}
