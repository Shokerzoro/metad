#include <iostream>
#include <map>
#include <exception>
#include <filesystem>
#include <tinyxml2.h>
#include <sys/inotify.h>
#include <signal.h>
#include <errno.h>

#include "meta.h"
#include "tstring.h"
#include "mainfunc.h"
#include "xmlfunc.h"

#define DIRMASK (IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_ONLYDIR)
#define FILEMASK (IN_MODIFY | IN_DELETE_SELF | IN_DONT_FOLLOW)

#define BYTE (size_t)1

using namespace tinyxml2;
using std::string;
using std::cout;
using std::cerr;
using std::endl;
using std::map;
using std::exception;
using Pair = std::pair<int, string>;
using IMap = std::map<int, string>;

using Path = std::filesystem::path;
using Direntry = std::filesystem::directory_entry;
using Diriter = std::filesystem::directory_iterator;

void inotify_loop(int infd, Path & target, IMap & mapper);
int watch_path(int infd, Direntry & newentry, uint32_t mask);
int clear_mapper(IMap & mapper);

static void sigalarm_hdl(int sigid); //Установка обработчика таймера
//Сохраняет с именем fullmeta-version
static void generate_fullxml(string & bldtime_str, string & proj_name, string & version, string & author_str);
static void update_inotify(void); //Апдейтим инотифай

static int upflag; // Флаг входа обработчика SIGALRM
extern Path target;
extern Path meta;

void full_metad_worker(Path & snap_dir, int alrmtime)
{
    int infd;
    char buf[4096];

    //Устанавливаем обработчик сигнала, инициируем inotify
    try {
        setup_sigalarm_handler(sigalarm_hdl);
        infd = inoinit();
    }
    catch(std::runtime_error & err)
    {
        cout << "runtime_error: " << err.what() << endl;
        exit(1);
    }

    IMap mapper;
    string build_time, project_name, version, author;
    Path metafile = target / "meta.XML";
    Path new_snap;

    //Проходим по всему таргету наблюдателем
    inotify_loop(infd, target, mapper);

    cout << "Full metadata configured." << endl;
    cout << "Stated at: " << get_current_time() << endl;
    cout << "Waiting events in target: " << target.string() << endl;

    while (true)
    { //Бесконечный цикл работы воркера

        #ifdef DEBUG_BUILD
        cout << "Making blocking reading." << endl;
        #endif

        //Тут чтение, которое либо прерывается сигналом, либо прочитывает события инотифай

        ssize_t readed = read(infd, buf, sizeof(buf));

        if (upflag == 1) //fullmeta generating
        {
            #ifdef DEBUG_BUILD
            cout << "Upflag is: " << upflag << " Time to generate data" << endl;
            #endif
            upflag = 0;

            try {
                get_meta(metafile, build_time, project_name, version, author);
            }
            catch(std::exception & ex)
            {
                //Логируем
                cerr << "Got exception getting metadata: " << ex.what() << endl;
                continue;
            }

            //Генерируем fullmeta xml и сохраняем
            generate_fullxml(build_time, project_name, version, author);

            //Копируем версию
            new_snap = snap_dir / version;
            if(std::filesystem::exists(new_snap))
            {
                cout <<  "Attempt to rewrite version: " <<  version << endl;
                continue;
            }

            std::filesystem::create_directory(new_snap);
            const auto CopyOptions = std::filesystem::copy_options::skip_symlinks | std::filesystem::copy_options::recursive;
            std::filesystem::copy(target, new_snap, CopyOptions);

            //Логируем
            cout << "version " <<  version << " duplicated to " <<  snap_dir <<  endl;

        }  //fullmeta generating
        else //inotify updating
        {
            for (char* ptr = buf; ptr < buf + readed;)
            { //Цикл чтения событий
                struct inotify_event* event = (struct inotify_event*)ptr;

                auto fiter = mapper.find(event->wd);
                if (fiter == mapper.end())
                {
                    ptr += sizeof(struct inotify_event) + event->len;
                    continue;
                }

                string base = fiter->second;
                if (!base.empty() && base.back() != '/') base += "/";

                if (event->mask & IN_MODIFY)
                {
                    alarm(alrmtime);
                    #ifdef DEBUG_BUILD
                    cout << "File modified: " << base << endl;
                    #endif
                }

                if (event->mask & IN_DELETE_SELF)
                {
                    #ifdef DEBUG_BUILD
                    cout << "File or dir deleted (self): " << base << endl;
                    #endif
                    mapper.erase(fiter);
                    clear_mapper(mapper);
                    alarm(alrmtime);
                }

                if (!(event->mask & IN_ISDIR))
                {
                    if (event->mask & IN_CREATE)
                    {
                        string fullname = base + event->name;
                        Direntry newentry(fullname);
                        int wd = watch_path(infd, newentry, FILEMASK);
                        if (wd != -1) mapper.insert(Pair(wd, fullname));
                        #ifdef DEBUG_BUILD
                        cout << "File created: " << fullname << endl;
                        #endif
                        alarm(alrmtime);
                    }
                } else
                {
                    if (event->mask & IN_CREATE)
                    {
                        string fullname = base + event->name;
                        Path newdir(fullname);
                        inotify_loop(infd, newdir, mapper);
                        #ifdef DEBUG_BUILD
                        cout << "Dir created: " << fullname << endl;
                        #endif
                        alarm(alrmtime);
                    }
                }

                ptr += sizeof(struct inotify_event) + event->len;
            }  //Цикл чтения событий
        }  //inotify updating
    } //Бесконечный цикл работы воркера
}

//Генерация и сохранение fullmetaxml
static void generate_fullxml(string & bldtime_str, string & proj_name, string & version, string & author_str)
{
    string docname = "full-meta-" + version + ".XML";
    Path fulldocname = meta / docname;

    //Логируем
    cout << "Update generating: " << version << endl;

    XMLDocument new_XML_doc;
    XMLElement* update = new_XML_doc.NewElement("update");
    new_XML_doc.InsertFirstChild(update);
    update->SetAttribute("build_time", bldtime_str.c_str());
    update->SetAttribute("project_name", proj_name.c_str());
    update->SetAttribute("version", version.c_str());
    update->SetAttribute("author", author_str.c_str());
    update->SetAttribute("filedir", target.string().c_str());

    Direntry target_dir(target);
    full_dmeta(update, target_dir, target);

    new_XML_doc.SaveFile(fulldocname.c_str());

    //Логируем
    cout << "Fullmetaxml file generated and saved: " <<  fulldocname << endl;
}

void sigalarm_hdl(int sigid)
{
    #ifdef DEBUG_BUILD
    cout << "I am handler: ";
    cout << "fot alarm signal: " << sigid << endl;
    cout << "Upflag was: " << upflag;
    #endif
    upflag = 1;
    #ifdef DEBUG_BUILD
    cout << ". Now its: " << upflag << endl;
    #endif // debug
}

