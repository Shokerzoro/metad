//
// Created by ivan on 8/8/25.
//

#ifndef METAD_TAGSTRRINGS_H
#define METAD_TAGSTRRINGS_H

struct TagStrings {
    static constexpr const char* PROTOCOL = "PROTOCOL";
    static constexpr const char* UNETMES = "UNET-MES";
    static constexpr const char* NOUPDATE = "NOUPDATE";
    static constexpr const char* SOMEUPDATE = "SOMEUPDATE";
    static constexpr const char* GETUPDATE = "GETUPDATE";
    static constexpr const char* VERSION = "VERSION";
    static constexpr const char* HASH = "HASH";

    static constexpr const char* NEWDIR = "NEWDIR";
    static constexpr const char* NEWFILE = "NEWFILE";
    static constexpr const char* DELFILE = "DELFILE";
    static constexpr const char* DELDIR = "DELDIR";

    static constexpr const char* AGREE = "AGREE";
    static constexpr const char* REJECT = "REJECT";
    static constexpr const char* COMPLETE = "COMPLETE";
    static constexpr const char* SERVERERROR = "SERVERERROR";
};


#endif //METAD_TAGSTRRINGS_H
