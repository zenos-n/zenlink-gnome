// SPDX-License-Identifier: MIT
#include "../host/pcvr_settings.hpp"
#include <iostream>
#include <sys/wait.h>
using namespace workspace::pcvr;
static void check(bool ok) { if(!ok) throw std::runtime_error("PC VR settings check failed"); }
int main() {
    g_unsetenv("QUEST_WORKSPACE_CONFIG");
    GError* error=nullptr;char* temp=g_dir_make_tmp("quest-pcvr-test-XXXXXX",&error);
    check(temp!=nullptr);fs::path root=temp;g_free(temp);
    g_setenv("XDG_CONFIG_HOME",(root/"config").c_str(),true);
    g_setenv("XDG_DATA_HOME",(root/"data").c_str(),true);
    fs::create_directories(root/"runtimes");
    auto wivrn=root/"runtimes/wivrn.json",steam=root/"runtimes/steam.json";
    std::ofstream(root/"runtimes/runtime.so")<<"test library placeholder";
    for(auto p:{wivrn,steam}) std::ofstream(p)<<"{\"runtime\":{\"library_path\":\"runtime.so\"}}";
    g_setenv("WORKSPACE_WIVRN_RUNTIME",wivrn.c_str(),true);
    g_setenv("WORKSPACE_STEAMVR_RUNTIME",steam.c_str(),true);
    g_setenv("XDG_CONFIG_DIRS",(root/"system-config").c_str(),true);
    auto settings=load();check(settings.overlay && settings.applications.empty());
    check(!fs::exists(active_runtime())); // Merely reading options cannot switch runtimes.
    fs::create_directories(active_runtime().parent_path());
    std::ofstream(active_runtime())<<"original custom runtime";
    set_default_runtime("wivrn");check(default_runtime()=="wivrn");
    check(text(active_runtime().string()+".quest-workspace-backup")=="original custom runtime");
    set_default_runtime("steamvr");check(default_runtime()=="steamvr");
    check(text(active_runtime().string()+".quest-workspace-backup")=="original custom runtime");
    bool rejected=false;try {set_default_runtime("malicious");}catch(...){rejected=true;}check(rejected && default_runtime()=="steamvr");
    auto argv=parse_command("/app/tool --name 'hello world' '$HOME' '`touch ignored`'");
    check(argv.size()==5 && argv[2]=="hello world" && argv[3]=="$HOME" && argv[4]=="`touch ignored`");
    bool empty_command=false;try{parse_command("''");}catch(...){empty_command=true;}check(empty_command);
    settings.overlay=false;settings.applications.push_back({"OVR Advanced Settings",{"steam","-applaunch","1009850"},"steamvr"});
    save(settings);auto restored=load();
    check(!restored.overlay && restored.applications.size()==1 && restored.applications[0].argv==settings.applications[0].argv);
    check(restored.applications[0].runtime=="steamvr");
    check(window_targets("")==std::vector<uint32_t>{0});
    check(window_targets("12,15")==std::vector<uint32_t>({12,15}));
    for(const char* bad:{"12,","12,,15","-1","4294967296","1,2,3,4,5","x"}) {bool bad_id=false;try{window_targets(bad);}catch(...){bad_id=true;}check(bad_id);}
    check(!mode_active());
    int lock=::open((directory()/"pcvr-session.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    check(lock>=0 && flock(lock,LOCK_EX|LOCK_NB)==0 && mode_active());
    rejected=false;try{set_default_runtime("wivrn");}catch(...){rejected=true;}check(rejected&&default_runtime()=="steamvr");
    ::close(lock);check(!mode_active());
    std::ofstream(directory()/"pcvr.json")<<"{\"applications\":[{\"name\":\"x\",\"runtime\":\"any\",\"argv\":[1]}]}";
    rejected=false;try {load();}catch(...){rejected=true;}check(rejected);
    fs::remove_all(root);
    std::cout<<"PASS runtime selection atomic backup, no read-side switch, argv preservation, settings roundtrip, malformed rejection, session exclusion\n";
}
