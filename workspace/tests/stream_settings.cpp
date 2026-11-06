// SPDX-License-Identifier: MIT
#include "../host/stream_settings.hpp"
#include <cassert>
int main() {
    gchar* directory=g_dir_make_tmp("zenlink-stream-test-XXXXXX",nullptr);assert(directory);
    g_setenv("QUEST_WORKSPACE_CONFIG",directory,true);
    workspace::StreamSettings low,high;low.quality=0;high.quality=100;
    assert(low.max_fps==120 && low.interval_us>=1000000.0/120);
    assert(low.inflight()<high.inflight());assert(low.bitrate(1920,1080)<high.bitrate(1920,1080));
    assert(high.bitrate(8192,8192)<=160000);
    assert(workspace::StreamSettings::load().quality==50);
    high.save();assert(workspace::StreamSettings::load().quality==100);
    low.save();assert(workspace::StreamSettings::load().quality==0);
    low.quality=101;bool rejected=false;try{low.save();}catch(...){rejected=true;}assert(rejected);
    assert(workspace::StreamSettings::load().quality==0);
    auto path=workspace::StreamSettings::path();g_remove(path.c_str());g_rmdir((std::string(directory)+"/quest-workspace").c_str());g_rmdir(directory);g_free(directory);
}
