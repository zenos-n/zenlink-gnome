// SPDX-License-Identifier: MIT
#include "pcvr_settings.hpp"
#include <glib-unix.h>
#include <openvr.h>
#include <signal.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <sys/wait.h>
using workspace::pcvr::Application;
using workspace::pcvr::Settings;

struct Session {
    GMainLoop* loop=nullptr;
    GDBusConnection* bus=nullptr;
    GPid server=0;
    std::vector<GPid> children;
    Settings settings;
    std::string runtime="wivrn",manifest,overlay,windows;
    bool entered=false,stopping=false,openvr=false;
    int result=0,lock=-1;
    gint64 deadline=g_get_monotonic_time()+180000000;
    ~Session() {
        if(openvr) vr::VR_Shutdown();
        if(lock>=0) {if(flock(lock,LOCK_EX|LOCK_NB)==0) std::filesystem::remove(workspace::pcvr::directory()/"pcvr-session.json");close(lock);}
        if(bus) g_object_unref(bus);
        if(loop) g_main_loop_unref(loop);
    }
    bool launch(const std::vector<std::string>& command,bool runtime_process=false) {
        std::vector<gchar*> args;for(const auto& arg:command) args.push_back(const_cast<char*>(arg.c_str()));args.push_back(nullptr);
        GError* error=nullptr;GPid pid=0;
        // Scope OpenXR selection to these applications as well as the user's default.
        gchar** env=g_get_environ();env=g_environ_setenv(env,"XR_RUNTIME_JSON",manifest.c_str(),true);
        if(!runtime_process) env=g_environ_setenv(env,"XDG_CONFIG_HOME",workspace::pcvr::config_root().c_str(),true);
        bool ok=g_spawn_async(nullptr,args.data(),env,GSpawnFlags(G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD),nullptr,nullptr,&pid,&error);
        g_strfreev(env);
        if(!ok) {g_warning("Could not launch %s: %s",command[0].c_str(),error->message);g_error_free(error);return false;}
        if(runtime_process) server=pid;else children.push_back(pid);
        g_child_watch_add(pid,+[](GPid pid,gint status,gpointer data) {
            auto& state=*static_cast<Session*>(data);
            if(pid==state.server) {
                state.server=0;
                if(!state.stopping && status) state.result=WIFEXITED(status)?WEXITSTATUS(status):1;
                state.stop();
            } else state.children.erase(std::remove(state.children.begin(),state.children.end(),pid),state.children.end());
            g_spawn_close_pid(pid);
        },this);
        return true;
    }
    void enter() {
        if(entered || stopping) return;
        entered=true;g_file_set_contents((workspace::pcvr::directory()/"pcvr-session.json").c_str(),"{\"connected\":true}",-1,nullptr);g_message("PCVR_ENTER runtime=%s",runtime.c_str());
        for(const auto& app:settings.applications) {
            if(app.runtime!="any" && app.runtime!=runtime) continue;
            g_message("Starting PC VR application: %s",app.name.c_str());launch(app.argv);
        }
        if(settings.overlay && !overlay.empty()) launch({overlay,"--runtime",runtime,"--windows",windows});
    }
    void disconnected() {if(entered) {g_message("PC VR headset disconnected");stop();}}
    void stop() {
        if(stopping) return;
        stopping=true;
        if(server) kill(server,SIGTERM);
        for(auto pid:children) kill(pid,SIGTERM);
        g_timeout_add(500,+[](gpointer data)->gboolean {
            auto& state=*static_cast<Session*>(data);
            if(!state.server && state.children.empty()) {g_main_loop_quit(state.loop);return G_SOURCE_REMOVE;}
            return G_SOURCE_CONTINUE;
        },this);
        g_timeout_add_seconds(5,+[](gpointer data)->gboolean {
            auto& state=*static_cast<Session*>(data);
            if(state.server) kill(state.server,SIGKILL);
            for(auto pid:state.children) kill(pid,SIGKILL);
            g_main_loop_quit(state.loop);return G_SOURCE_REMOVE;
        },this);
    }
    void changed(GVariant* properties) {
        gboolean value;
        if(!g_variant_lookup(properties,"HeadsetConnected","b",&value)) return;
        if(value) enter();else disconnected();
    }
    void check_steam() {
        if(!openvr) {
            vr::EVRInitError error=vr::VRInitError_None;
            vr::VR_Init(&error,vr::VRApplication_Utility);
            if(error!=vr::VRInitError_None) return;
            openvr=true;
        }
        if(vr::VRSystem()->IsTrackedDeviceConnected(vr::k_unTrackedDeviceIndex_Hmd)) enter();else disconnected();
    }
};
int main(int argc,char** argv) {
    gchar* runtime=nullptr;gchar* runtime_json=nullptr;gchar* server=nullptr;gchar* config=nullptr;gchar* overlay=nullptr;gchar* steam=nullptr;gchar* windows=nullptr;
    GOptionEntry options[]={
        {"runtime",0,0,G_OPTION_ARG_STRING,&runtime,"WiVRn or SteamVR","ID"},
        {"runtime-json",0,0,G_OPTION_ARG_FILENAME,&runtime_json,"OpenXR runtime manifest","FILE"},
        {"server",0,0,G_OPTION_ARG_FILENAME,&server,"WiVRn executable","FILE"},
        {"server-config",0,0,G_OPTION_ARG_FILENAME,&config,"WiVRn session configuration","FILE"},
        {"overlay",0,0,G_OPTION_ARG_FILENAME,&overlay,"Workspace overlay executable","FILE"},
        {"steam",0,0,G_OPTION_ARG_FILENAME,&steam,"Steam launcher","FILE"},
        {"windows",0,0,G_OPTION_ARG_STRING,&windows,"Selected workspace window IDs","IDS"},{nullptr,0,0,G_OPTION_ARG_NONE,nullptr,nullptr,nullptr}};
    GError* error=nullptr;auto* context=g_option_context_new("— on-demand PC VR supervisor");g_option_context_add_main_entries(context,options,nullptr);
    if(!g_option_context_parse(context,&argc,&argv,&error)) {g_printerr("%s\n",error->message);g_error_free(error);g_option_context_free(context);return 2;}
    g_option_context_free(context);
    Session state;
    try {
        if(!runtime || !workspace::pcvr::runtime_id(runtime)) throw std::runtime_error("Choose --runtime wivrn or steamvr");
        state.runtime=runtime;state.settings=workspace::pcvr::load();state.manifest=runtime_json?runtime_json:workspace::pcvr::manifest(runtime).string();
        if(state.manifest.empty() || !std::filesystem::is_regular_file(state.manifest)) throw std::runtime_error("Selected OpenXR runtime is unavailable");
        if(overlay) state.overlay=overlay;
        state.windows=windows?windows:"";workspace::pcvr::window_targets(state.windows);
        std::filesystem::create_directories(workspace::pcvr::directory());
        state.lock=open((workspace::pcvr::directory()/"pcvr-session.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
        if(state.lock<0 || flock(state.lock,LOCK_EX|LOCK_NB)<0) throw std::runtime_error("PC VR is already active");
        g_file_set_contents((workspace::pcvr::directory()/"pcvr-session.json").c_str(),"{\"connected\":false}",-1,nullptr);
        state.bus=g_bus_get_sync(G_BUS_TYPE_SESSION,nullptr,&error);
        if(!state.bus) {std::string reason=error->message;g_clear_error(&error);throw std::runtime_error(reason);}
        state.loop=g_main_loop_new(nullptr,false);
        if(state.runtime=="wivrn") {
            if(!server || !config) throw std::runtime_error("WiVRn executable and configuration are required");
            g_dbus_connection_signal_subscribe(state.bus,"io.github.wivrn.Server","org.freedesktop.DBus.Properties","PropertiesChanged",
                "/io/github/wivrn/Server","io.github.wivrn.Server",G_DBUS_SIGNAL_FLAGS_NONE,
                +[](GDBusConnection*,const char*,const char*,const char*,const char*,GVariant* args,gpointer data) {
                    GVariant* changed=g_variant_get_child_value(args,1);static_cast<Session*>(data)->changed(changed);g_variant_unref(changed);
                },&state,nullptr);
            // Initial property snapshot also covers a runtime that connects before its first signal.
            g_bus_watch_name_on_connection(state.bus,"io.github.wivrn.Server",G_BUS_NAME_WATCHER_FLAGS_NONE,
                +[](GDBusConnection* bus,const char*,const char*,gpointer data) {
                    g_dbus_connection_call(bus,"io.github.wivrn.Server","/io/github/wivrn/Server","org.freedesktop.DBus.Properties","Get",
                        g_variant_new("(ss)","io.github.wivrn.Server","HeadsetConnected"),G_VARIANT_TYPE("(v)"),G_DBUS_CALL_FLAGS_NONE,1000,nullptr,
                        +[](GObject* source,GAsyncResult* result,gpointer data) {
                            GError* error=nullptr;GVariant* reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&error);
                            if(reply) {GVariant* box=g_variant_get_child_value(reply,0);GVariant* value=g_variant_get_variant(box);
                                if(g_variant_is_of_type(value,G_VARIANT_TYPE_BOOLEAN)&&g_variant_get_boolean(value)) static_cast<Session*>(data)->enter();
                                g_variant_unref(value);g_variant_unref(box);g_variant_unref(reply);}
                            g_clear_error(&error);
                        },data);
                },nullptr,&state,nullptr);
            if(!state.launch({server,"--no-manage-active-runtime","-f",config},true)) throw std::runtime_error("WiVRn could not start");
        } else {
            // Utility initialization does not start SteamVR; Steam owns its server lifecycle.
            state.check_steam();
            if(!state.openvr && !state.launch({"systemd-run","--user","--collect","--unit=quest-workspace-steam-launch","--",steam?steam:"steam","-applaunch","250820"})) throw std::runtime_error("SteamVR could not start");
        }
        g_timeout_add_seconds(2,+[](gpointer data)->gboolean {
            auto& state=*static_cast<Session*>(data);
            if(state.stopping) return G_SOURCE_CONTINUE;
            if(state.runtime=="steamvr") state.check_steam();
            if(!state.entered && g_get_monotonic_time()>state.deadline) {g_message("No headset connected within180seconds");state.stop();}
            return G_SOURCE_CONTINUE;
        },&state);
        auto stop=+[](gpointer data)->gboolean {static_cast<Session*>(data)->stop();return G_SOURCE_CONTINUE;};
        g_unix_signal_add(SIGTERM,stop,&state);g_unix_signal_add(SIGINT,stop,&state);
        g_main_loop_run(state.loop);
    } catch(const std::exception& e) {g_printerr("%s\n",e.what());return 1;}
    g_free(runtime);g_free(runtime_json);g_free(server);g_free(config);g_free(overlay);g_free(steam);g_free(windows);
    return state.result;
}
