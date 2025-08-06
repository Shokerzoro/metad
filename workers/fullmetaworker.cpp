#include <iostream>
#include <map>
#include <exception>
#include <filesystem>
#include <tinyxml2.h>
#include <sys/inotify.h>
#include <signal.h>

#include "../meta.h"
#include "../tstring.h"
#include "../mainfunc.h"
#include "../xmlfunc.h"

#define DIRMASK (IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_ONLYDIR)
#define FILEMASK (IN_MODIFY | IN_DELETE_SELF | IN_DONT_FOLLOW)

#define BYTE (size_t)1

using namespace tinyxml2;
using Pair = std::pair<int, std::string>;
using IMap = std::map<int, std::string>;
using Path = std::filesystem::path;
using Direntry = std::filesystem::directory_entry;
using Diriter = std::filesystem::directory_iterator;

void inotify_loop(int infd, Path & target, IMap & mapper);
int watch_path(int infd, Direntry & newentry, uint32_t mask);
int clear_mapper(IMap & mapper);

static void sigalarm_hdl(int sigid); //Установка обработчика таймера
//Сохраняет с именем fullmeta-version
static void generate_fullxml(std::string & bldtime_str, std::string & proj_name, std::string & version, std::string & author_str);
static void update_inotify(void); //Апдейтим инотифай

static int upflag; // Флаг входа обработчика SIGALRM
extern Path target;
extern Path meta;
// ... другие include'ы
static bool ignore_meta_delete = false;
static bool ignore_meta_modify = false; // Для подавления IN_MODIFY на meta.XML при ручном удалении

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
        std::cout << "runtime_error: " << err.what() << std::endl;
        exit(1);
    }

    //Необходимые переменные
    std::string build_time, project_name, version, author; //Получаем из meta.xml
    IMap mapper; //Контейнер путь/inotify obj
    Path metafile = target / "meta.XML";
    Path new_snap;

    //Проходим по всему таргету наблюдателем
    inotify_loop(infd, target, mapper);

    std::cout << "Full metadata configured." << std::endl;
    std::cout << "Stated at: " << get_current_time() << std::endl;
    std::cout << "Waiting events in target: " << target.string() << std::endl;

    while (true)
    { //Бесконечный цикл работы воркера

        //Тут чтение, которое либо прерывается сигналом, либо прочитывает события инотифай
        ssize_t readed = read(infd, buf, sizeof(buf));

        if (upflag == 1) //fullmeta generating
        {
            #ifdef DEBUG_BUILD
            std::cout << "Time to generate new version" << std::endl;
            #endif
            upflag = 0;

            try {
                //Получаем метаданные о новой версии
                get_meta(metafile, build_time, project_name, version, author);
                //Генерируем fullmeta xml и сохраняем
                generate_fullxml(build_time, project_name, version, author);

                //Делаем снэп новой версии
                new_snap = snap_dir / version;
                if(std::filesystem::exists(new_snap))
                    throw std::invalid_argument("Attempt to rewrite version");
                std::filesystem::create_directory(new_snap);
                const auto CopyOptions = std::filesystem::copy_options::skip_symlinks | std::filesystem::copy_options::recursive;
                std::filesystem::copy(target, new_snap, CopyOptions);
                std::cout << "version " <<  version << " duplicated to " <<  snap_dir <<  std::endl;

                // Удаляем meta.XML и устанавливаем флаги игнорирования
                ignore_meta_delete = true;
                ignore_meta_modify = true;
                std::filesystem::remove(metafile);

            }
            catch(std::exception & ex)
            {
                std::cerr << "WARNING: error occured" << std::endl;
                std::cerr << "Got exception while generating fullxml: " << ex.what() << std::endl;
                continue;
            }

            std::cout << "Fullmeta of project " << project_name << "generated" << std::endl;
            std::cout << "Current version: " << version << " build by " << author << std::endl;

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

                std::string base = fiter->second;
                if (!base.empty() && base.back() != '/') base += "/";

                if (event->mask & IN_MODIFY)
                {
                    if (ignore_meta_modify && base == metafile.string()) {
                        std::cout << "IN_MODIFY ignored for meta.XML (manual delete in progress)" << std::endl;
                    } else if (std::filesystem::exists(base)) {
                        std::cout << "File modified: " << base << std::endl;
                        alarm(alrmtime);
                    } else {
                        std::cout << "IN_MODIFY ignored for deleted file: " << base << std::endl;
                    }
                }

                if (event->mask & IN_DELETE_SELF)
                {
                    std::cout << "File or dir deleted (self): " << base << std::endl;

                    if (ignore_meta_delete && base == metafile.string()) {
                        std::cout << "meta.XML deletion ignored (manual)" << std::endl;
                        ignore_meta_delete = false;
                        ignore_meta_modify = false;
                    } else {
                        alarm(alrmtime);
                    }

                    mapper.erase(fiter);
                    clear_mapper(mapper);
                }

                if (!(event->mask & IN_ISDIR))
                {
                    if (event->mask & IN_CREATE)
                    {
                        std::string fullname = base + event->name;
                        Direntry newentry(fullname);
                        int wd = watch_path(infd, newentry, FILEMASK);
                        if (wd != -1) mapper.insert(Pair(wd, fullname));
                        std::cout << "File created: " << fullname << std::endl;
                        alarm(alrmtime);
                    }
                } else
                {
                    if (event->mask & IN_CREATE)
                    {
                        std::string fullname = base + event->name;
                        Path newdir(fullname);
                        inotify_loop(infd, newdir, mapper);
                        std::cout << "Dir created: " << fullname << std::endl;
                        alarm(alrmtime);
                    }
                }

                ptr += sizeof(struct inotify_event) + event->len;
            }  //Цикл чтения событий
        }  //inotify updating
    } //Бесконечный цикл работы воркера
}


//Генерация и сохранение fullmetaxml
static void generate_fullxml(std::string & bldtime_str, std::string & proj_name, std::string & version, std::string & author_str)
{
    std::string docname = "full-meta-" + version + ".XML";
    Path fulldocname = meta / docname;

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
    std::cout << "Fullmetaxml file generated and saved: " <<  fulldocname << std::endl;
}

void sigalarm_hdl(int sigid)
{
    #ifdef DEBUG_BUILD
    std::cout << "Fot alarm signal: " << sigid << std::endl;
    #endif
    upflag = 1;
}

