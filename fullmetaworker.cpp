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

#define DIRMASK (IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_ONLYDIR)
#define FILEMASK (IN_MODIFY | IN_DELETE_SELF | IN_DONT_FOLLOW)

#define BYTE (size_t)1
#define ALARMTIME 30

using namespace tinyxml2;
using std::string;
using std::cout;
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

void sigalarm_hdl(int sigid);
int upflag; // Флаг входа обработчика SIGALRM

extern Path target;
extern Path meta;

void full_metad_worker(void)
{
    struct sigaction sa {};
    sa.sa_handler = sigalarm_hdl;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGALRM, &sa, nullptr) == -1)
    {
        cout << "ALARM sigaction error" << strerror(errno) << endl;
        exit(1);
    }

    int infd = inotify_init();
    if (infd == -1)
    {
        cout << "Inotify init error" << endl;
        exit(1);
    }

    IMap mapper;
    inotify_loop(infd, target, mapper);
    upflag = 0;

    #ifdef DEBUG_BUILD
    cout << "Into eternal loooooooooooooooooop" << endl;
    #endif

    while (true)
    {
        #ifdef DEBUG_BUILD
        cout << "Making blocking reading." << endl;
        #endif

        char buf[4096];
        ssize_t readed = read(infd, buf, sizeof(buf));

        cout << "Full metadata configured." << endl;
        cout << "Stated at: " << get_current_time() << endl;
        cout << "Waitung events in target: " << target.string() << endl;

        if (upflag == 1)
        {
            #ifdef DEBUG_BUILD
            cout << "upflag is: " << upflag << " Time to generate data" << endl;
            #endif
            upflag = 0;

            string date = get_current_time();
            string docname = "full-meta-" + date + ".XML";
            string fulldocname = meta.string() + docname;

            //Логируем
            cout << "Update generating: " << date << endl;

            XMLDocument new_XML_doc;
            XMLElement* update = new_XML_doc.NewElement("update");
            new_XML_doc.InsertFirstChild(update);
            update->SetAttribute("date", date.c_str());
            update->SetAttribute("filedir", target.string().c_str());

            Direntry target_dir(target);
            full_dmeta(update, target_dir, target);

            new_XML_doc.SaveFile(fulldocname.c_str());
            continue;
        }
        for (char* ptr = buf; ptr < buf + readed;)
        {
            struct inotify_event* event = (struct inotify_event*)ptr;

            #ifdef DEBUG_BUILD
            cout << "inotify event: wd=" << event->wd
                 << ", mask=0x" << std::hex << event->mask
                 << (event->len ? ", name=" + string(event->name) : "")
                 << std::dec << endl;
            #endif

            auto fiter = mapper.find(event->wd);
            if (fiter == mapper.end())
            {
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            }

            string base = fiter->second;
            if (!base.empty() && base.back() != '/') base += "/";

            if (event->mask & IN_MODIFY) {
                alarm(ALARMTIME);
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
                alarm(ALARMTIME);
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
                    alarm(ALARMTIME);
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
                    alarm(ALARMTIME);
                }
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }
    }
}

void inotify_loop(int infd, Path & target, IMap & mapper)
{
    #ifdef DEBUG_BUILD
    cout << "Inotify request: " << target.string() << endl;
    #endif
    Direntry newdir(target);
    if (newdir.is_directory() && !newdir.is_symlink())
    {
        int newfd = watch_path(infd, newdir, DIRMASK);
        if (newfd != -1 && mapper.find(newfd) == mapper.end())
        {
            #ifdef DEBUG_BUILD
            cout << "Its new directory" << endl;
            #endif
            mapper.insert(Pair(newfd, target.string()));
        }

        for (Diriter curriter(target); curriter != end(curriter); curriter++)
        {
            if (curriter->is_regular_file() && !curriter->is_symlink())
            {
                Direntry newfile(*curriter);
                #ifdef DEBUG_BUILD
                cout << "New entry is file: " << newfile.path().string() << endl;
                #endif
                int newfd = watch_path(infd, newfile, FILEMASK);
                if (newfd != -1 && mapper.find(newfd) == mapper.end()) {
                    mapper.insert(Pair(newfd, (*curriter).path().string()));
                }
            }
            if (curriter->is_directory() && !curriter->is_symlink())
            {
                Path newpath((*curriter).path());
                Direntry newdirloop(newpath);
                #ifdef DEBUG_BUILD
                cout << "New entry is directory: " << newdirloop.path().string() << endl;
                #endif
                inotify_loop(infd, newpath, mapper);
            }
        }
    }
}

int watch_path(int infd, Direntry & newentry, uint32_t mask)
{
    #ifdef DEBUG_BUILD
    cout << "new watching request: " << newentry.path().string() << endl;
    #endif
    int retvalue = inotify_add_watch(infd, newentry.path().string().c_str(), mask);
    #ifdef DEBUG_BUILD
    if (retvalue == -1)
    {
        cout << "Add watch error: " << newentry.path().string() << endl;
        perror("inotify_add_watch");
    } else
    {
        cout << "Added new inotify object: " << newentry.path().string() << endl;
    }
    #endif
    return retvalue;
}

int clear_mapper(IMap & mapper)
{
    int cleared = 0;
    auto iter = mapper.begin();
    while (iter != mapper.end())
    {
        auto nextiter = std::next(iter);
        if (!std::filesystem::exists(iter->second))
        {
            mapper.erase(iter);
            cleared++;
        }
        iter = nextiter;
    }
    return cleared;
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
