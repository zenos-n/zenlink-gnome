// SPDX-License-Identifier: MIT
#pragma once
#include <glib.h>
#include <glib/gstdio.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
namespace workspace {
struct StreamSettings {
    unsigned quality=50;
    static constexpr unsigned max_fps=120;
    static constexpr gint64 interval_us=(G_USEC_PER_SEC+max_fps-1)/max_fps;
    unsigned inflight() const { return quality<35?1:2; }
    unsigned bitrate(unsigned width,unsigned height) const {
        double scale=std::clamp(double(width)*height/(1920.0*1080.0),0.25,2.0);
        return unsigned(std::clamp((8000.0+720.0*quality)*scale,2000.0,160000.0));
    }
    static std::string path() {
        const char* root=g_getenv("QUEST_WORKSPACE_CONFIG");
        return std::string(root?root:g_get_user_config_dir())+"/quest-workspace/stream.ini";
    }
    static StreamSettings load() {
        StreamSettings value;if(!g_file_test(path().c_str(),G_FILE_TEST_EXISTS))return value;
        auto* file=g_key_file_new();GError* error=nullptr;
        bool loaded=g_key_file_load_from_file(file,path().c_str(),G_KEY_FILE_NONE,&error);
        int quality=loaded?g_key_file_get_integer(file,"stream","quality",&error):-1;
        g_key_file_unref(file);
        if(error || quality<0 || quality>100){g_clear_error(&error);throw std::runtime_error("Invalid streaming quality setting");}
        value.quality=quality;return value;
    }
    void save() const {
        if(quality>100)throw std::runtime_error("Streaming quality must be between0 and100");
        auto name=path();auto directory=name.substr(0,name.find_last_of('/'));g_mkdir_with_parents(directory.c_str(),0700);
        auto text="[stream]\nquality="+std::to_string(quality)+"\n";GError* error=nullptr;
        if(!g_file_set_contents_full(name.c_str(),text.c_str(),text.size(),G_FILE_SET_CONTENTS_CONSISTENT,0600,&error)) {
            std::string reason=error->message;g_clear_error(&error);throw std::runtime_error(reason);
        }
    }
};
}
