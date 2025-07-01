
/*  Utility/Daemon for meta data generation
Утилита, которая сгенерирует мета данные о каталоге / вычислит дельту до самой актуальной версии файлов
1. full-meta-"date".XML - содержит полную информацию структуре каталога.
2. delta-meta-"date".XML - содержит дельту изменений между указанным актуальным full-meta файлами
Принимает аргументы
[1]-full / %version% полные или дельта, обязательно (во втором случае строка - версия относительно которой формируется дельта)
[2]-target_dir путь к каталогу для генерации, обязательно (для дельты в нем должны находится full-meta документы а не каталог для генерации)
[3]-meta_dir путь к каталогу сохранения XML, по умолчанию target_dir (не обязательно)

Демон предназначен для автогенерации full-meta, отслеживает изменения в target-dir.
Демон предназначен для генерации delta-meta по запросу (TCP) и отправки клиенту
Будет автом. генерировать meta данные, delta по запросу
Принимает аргументы:
[1]-full/delta как будет отрабатывать, обязательно
[2]-target_dir путь к каталогу для генерации, обязательно (для дельты в нем должны находится full-meta документы а не каталог для генерации)
[3]-meta путь сохранения
[4]-demonize
[5]-IPv4 адрес в формате 1111.2222.3333.444 (для delta обязательно)
[6]-Порт прослушивания (для delta обязательно)
*/

#include <iostream>
#include <string>
#include <filesystem>
#include <tinyxml2.h>
#include <exception>
#include <list>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>

#include "meta.h"
#include "tstring.h"
#include "xmlfunc.h"
#include "mainfunc.h"
#include "threaddatacontainer.h"

using namespace tinyxml2;
using std::string;
using std::cout;
using std::cerr;
using std::endl;
using std::exception;
using Path = std::filesystem::path;
using Direntry = std::filesystem::directory_entry;
using ThreadList = std::list<ThreadDataContainer*>;

extern void full_metad_worker(void);
extern void* delta_metad_worker(void* data);
extern void become_daemon(string logpath);

//Общие для всех переменные
Path target;
Path meta;

int main(int argc, char** argv)
{
    if(argc == 3 || argc == 4) //Отработка как утилита без -demonize
    {
        //Строки для дальнейшей работы
        string date = get_current_time(); //Дата
        string calltype = argv[1]; //Тип вызова
        target = Path(argv[2]); //Целевая директория
        if (target.string().back() != '/') { target += '/'; }
        meta = target; //Директория сохранения метаданных, по умолчанию таргет
        string docname; //Имя нового документа в зависимости от вызова
        string fulldocname; //Полное имя документа

        if(argc == 4) //Изменение директории по-умолчанию
        { meta = Path(argv[3]); if (meta.string().back() != '/') { meta += '/'; } }

        //Проверка существования каталогов
        if(!std::filesystem::exists(target))
        { cout << "Error target directory path: " << target << endl; exit(1); }
        if(!std::filesystem::exists(meta))
        { cout << "Error target directory path: " << meta << endl; exit(1); }

        #ifdef DEBUG_BUILD
        cout << "Calltype: " << calltype << endl;
        cout << "Target directory: " << target << endl;
        cout << "Meta directory: " << meta << endl;
        #endif // debug

        ////Создание XML документа и корневого элемента
        XMLDocument new_XML_doc;
        XMLElement* update = new_XML_doc.NewElement("update");
        new_XML_doc.InsertFirstChild(update);

        //Разделение алгоритма работы
        if(calltype == "full") //Одноразовая генерация full-meta
        {
            //Дописываем атрибуты
            update->SetAttribute("date", date.c_str());
            update->SetAttribute("filedir", target.string().c_str());

            //Формируем полное имя документа
            docname = "full-meta-" + date + ".XML";
            fulldocname = meta.string() + docname;

            //Входим в рекурсию
            Direntry target_dir(target);
            full_dmeta(update, target_dir, target);
        }
        else //Одноразовая генерация дельты
        {
            //Полные пути до старого и актуального документов
            string actualdate;
            string old_meta_name = (target.string() + "full-meta-" + calltype + ".XML");
            string actual_meta_name = get_actual(target, actualdate);

            if(!std::filesystem::exists(old_meta_name))
            { cout << "Error old meta XML doc path: " << old_meta_name << endl; exit(1); }
            if(!std::filesystem::exists(actual_meta_name))
            { cout << "Error actual meta XML doc path: " << actual_meta_name << endl; exit(1); }

            #ifdef DEBUG_BUILD
            cout << "Old full-meta XML doc: " << old_meta_name << endl;
            cout << "Actual full-meta XML doc: " << actual_meta_name << endl;
            #endif // debug

            //Формируем имя файла и ищем, если существует
            docname = "delta-meta-" + calltype + "-" + actualdate + ".XML";
            fulldocname = meta.string() + docname;
            if(std::filesystem::exists(fulldocname))
            { cout << "Delta XML already exists: " << fulldocname << endl; exit(0); }

            XMLDocument old_XML_doc;
            XMLDocument actual_XML_doc;
            #ifdef DEBUG_BUILD
            cout << "Old full-meta opening" << endl;
            #endif // debug(struct sockaddr*)

            open_XML_doc(old_XML_doc, old_meta_name.c_str());
            #ifdef DEBUG_BUILD
            cout << "Actual full-meta opening" << endl;
            #endif // debug
            open_XML_doc(actual_XML_doc, actual_meta_name.c_str());

            //Открывает root элементы и добавляем аттрибут filedir
            XMLElement* oldupdate = old_XML_doc.RootElement();
            XMLElement* actualupdate = actual_XML_doc.RootElement();
            const char* actualfiledir = actualupdate->Attribute("filedir");
            update->SetAttribute("date", actualdate.c_str());
            update->SetAttribute("filedir", actualfiledir);

            //Вызываем рекурсивную функцию
            delta_dmeta(oldupdate, actualupdate, update);
        }

        //Сохранение
        #ifdef DEBUG_BUILD
        cout << "Saving new file: " << fulldocname << endl;
        #endif // DEBUG_BUILD
        new_XML_doc.SaveFile(fulldocname.c_str());

    } else if (((argc = 5) || (argc = 7)) && (strcmp(argv[4], "demonize") == 0)) //Отработка как демон
    {
        #ifdef DEBUG_BUILD
        cout << "Demon starts" << endl;
        #endif // DEBUG_BUILD

        //Демонизация, нужно очень внимательно следить за открытыми файлами и утечкой памяти
        #ifndef DEBUG_BUILD
        string logpath("/var/log/metad/logfile.txt");
        become_daemon(logpath);
        #endif // release

        //Необходимые для работы строки
        string calltype = argv[1]; //Тип вызова full/delta
        target = Path(argv[2]); //Целевая директория
        if(!std::filesystem::exists(target))
        { cout << "Error target directory: " << target << endl; exit(1); }
        meta = Path(argv[3]); //Директория сохранения метаданных
        if(!std::filesystem::exists(meta))
        { cout << "Error meta directory: " << target << endl; exit(1); }

        if(calltype == "full") //Демон full-meta
        {
            #ifdef DEBUG_BUILD
            cout << "FULL META DAEMON" << endl;
            #endif // DEBUG_BUILD

            full_metad_worker(); //Бесконечная работа демонаъ

		} //Демон full-meta
        if(calltype == "delta") //Демон delta-meta
        {
            #ifndef DEBUG_BUILD
            //Получаем айпи адрес и порт для сокета
            string ipv4(argv[5]);
            string portstr = argv[6];
            int port = -1;
            try { port = std::stoi(portstr); }
            catch(exception & ex)
            { cout << "Port arg error" << ex.what() << endl; exit(1); }
            if((port <= 0) || (port > UINT16_MAX))
            { cout << "Wrong port num: " << port << endl; exit(1); }
            #endif // DEBUG_BUILD
            #ifdef DEBUG_BUILD
            string ipv4("127.0.0.1");
            int port = 7783;
            #endif // DEBUG_BUILD

            int serversock, workersock;
            if((serversock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
            { cerr << "Socket creation error: " << strerror(errno) << endl;
            cerr << "Check IPv4: " << ipv4 << endl; exit(1); }

            //Создаем сокет, биндим и отмечаем как пассивный
            //Здесь нелбходимо добавить шифрование общения. Будет в конечной версии
            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);
            server_addr.sin_addr.s_addr = inet_addr(ipv4.c_str());

            if(bind(serversock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) == -1)
            { cerr << "Socket binding error: " << strerror(errno) << endl; exit(1); }
            if(listen(serversock, 256) == -1)
            { cerr << "Socket listening error" << strerror(errno) << endl; exit(1); }

            //Создаем сервак и потокобезопасную структуру для обмена инфой
            pthread_t nthreadt;
            int newworkers = 0;
            ThreadList threads;

            cout << "Configured. Started at:" << get_current_time() << endl;
            cout << "Listening: " << ipv4 << ":" << port << endl;
            cout << "Waiting incoming connections" << endl;


            while(workersock = accept(serversock, nullptr, nullptr)) //Бесконечный цикл обслуживания клиентов
            {
                #ifdef DEBUG_BUILD
                cout << "Got acception" << endl;
                #endif // DEBUG_BUILD

                ThreadDataContainer* nthreadd;
                try { nthreadd = new ThreadDataContainer(workersock); }
                catch(exception & ex) { shutdown(workersock, SHUT_RDWR); cerr << "Container baddalloc: " << ex.what() << endl; close(workersock); continue; }

                //Создание потока
                if(pthread_create(&nthreadt, nullptr, delta_metad_worker, nthreadd) != 0)
                {
                    shutdown(workersock, SHUT_RDWR);
                    cerr << "Thread creation error" << strerror(errno) << endl;
                    delete nthreadd;
                    close(workersock);
                    continue;
                }
                else //Все структуры готовы
                {
                    threads.push_back(nthreadd);
                    newworkers++;
                }

                #ifdef DEBUG_BUILD
                cout << "Thread created, thread_id: " << nthreadt << endl;
                #endif //DEBUG_BUILD

                for (auto iter = threads.begin(); iter != threads.end();) //Чистим систему от данных
                {
                    ThreadDataContainer* thrd = *iter;
                    pthread_t thrdid = thrd->checkrun();
                    if(thrdid != (pthread_t)-1) //значит вернул настоящий pthread
                    { auto deliter = iter; iter++; pthread_join(thrdid, nullptr); threads.erase(deliter); delete thrd; }
                    else
                    { iter++; }
                }

                #ifdef DEBUG_BUILD
                cout << "Waiting next acception" << endl;
                #endif // DEBUG_BUILD

            } //Бесконечный цикл обслуживания клиентов
        } //Демон delta-meta
    }//Отработка как демон
    else
    { cout << "Error args" << endl; }
    return 0;
} //Main



