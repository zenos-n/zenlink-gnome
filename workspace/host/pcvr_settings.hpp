// SPDX-License-Identifier: MIT
#pragma once
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include <stdexcept>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

namespace workspace::pcvr {
namespace fs=std::filesystem;
struct Application { std::string name; std::vector<std::string> argv; std::string runtime="any"; };
struct Settings { bool overlay=true; std::vector<Application> applications; };
inline fs::path config_root() {
    if(const char* root=g_getenv("QUEST_WORKSPACE_CONFIG")) return root;
    return g_get_user_config_dir();
}
inline fs::path directory() { return config_root()/"quest-workspace"; }
inline fs::path active_runtime() { return config_root()/"openxr/1/active_runtime.json"; }
inline bool mode_active();
inline bool runtime_id(const std::string& id) { return id=="wivrn" || id=="steamvr"; }
inline std::string text(const fs::path& path) {
    std::ifstream stream(path); if (!stream) return {};
    std::string value((std::istreambuf_iterator<char>(stream)),{});
    if (value.size()>1024*1024) throw std::runtime_error("Settings file is too large");
    return value;
}
inline std::vector<fs::path> steam_roots() {
    std::vector<fs::path> roots={fs::path(g_get_user_data_dir())/"Steam",fs::path(g_get_home_dir())/".steam/steam"};
    if (const char* path=g_getenv("WORKSPACE_STEAM_ROOT")) roots.insert(roots.begin(),path);
    auto initial=roots;
    const std::regex pattern(R"vdf("path"\s*"((?:\\.|[^"\\])*)")vdf");
    for (const auto& root:initial) {
        auto data=text(root/"steamapps/libraryfolders.vdf");
        for (std::sregex_iterator i(data.begin(),data.end(),pattern),end;i!=end;++i) {
            std::string encoded=(*i)[1],path;
            for(size_t j=0;j<encoded.size();++j) {
                if(encoded[j]=='\\' && j+1<encoded.size()) ++j;
                path+=encoded[j];
            }
            if (fs::path(path).is_absolute()) roots.emplace_back(path);
        }
    }
    return roots;
}
inline fs::path manifest(const std::string& id) {
    if (!runtime_id(id)) throw std::runtime_error("Unknown OpenXR runtime");
    const char* override=g_getenv(id=="wivrn"?"WORKSPACE_WIVRN_RUNTIME":"WORKSPACE_STEAMVR_RUNTIME");
    if (override && fs::is_regular_file(override)) return override;
    if(id=="steamvr") {
        for(const auto& root:steam_roots()) {
            auto path=root/"steamapps/common/SteamVR/steamxr_linux64.json";
            if(fs::is_regular_file(path)) return path;
        }
    } else {
        for(auto path:{fs::path("/run/current-system/sw/share/openxr/1/openxr_wivrn.json"),
                       fs::path(g_get_home_dir())/".nix-profile/share/openxr/1/openxr_wivrn.json",
                       fs::path("/usr/share/openxr/1/openxr_wivrn.json")})
            if(fs::is_regular_file(path)) return path;
    }
    return {};
}
inline std::string default_runtime() {
    auto active=active_runtime();
    if(!fs::exists(active)) {
        for(const char* const* dir=g_get_system_config_dirs();*dir;++dir) {
            auto candidate=fs::path(*dir)/"openxr/1/active_runtime.json";
            if(fs::exists(candidate)){active=candidate;break;}
        }
        if(!fs::exists(active)&&fs::exists("/etc/openxr/1/active_runtime.json"))active="/etc/openxr/1/active_runtime.json";
        if(!fs::exists(active))return "wivrn";
    }
    for(const auto& id:{"wivrn","steamvr"}) {
        auto path=manifest(id); std::error_code error;
        if(!path.empty() && fs::equivalent(active,path,error)) return id;
    }
    auto data=text(active);
    if(data.find("wivrn")!=std::string::npos) return "wivrn";
    if(data.find("SteamVR")!=std::string::npos || data.find("steamvr")!=std::string::npos) return "steamvr";
    return "custom";
}
inline void set_default_runtime(const std::string& id) {
    if(mode_active()) throw std::runtime_error("End PC VR before changing the default runtime");
    auto path=manifest(id);
    if(path.empty()) throw std::runtime_error(id=="steamvr"?"SteamVR is not installed":"WiVRn is not installed");
    GError* error=nullptr; JsonParser* parser=json_parser_new();
    bool valid=json_parser_load_from_file(parser,path.c_str(),&error);
    if(valid) {
        auto* root=json_parser_get_root(parser);
        valid=JSON_NODE_HOLDS_OBJECT(root) && json_object_has_member(json_node_get_object(root),"runtime");
        if(valid) {
            auto* runtime_node=json_object_get_member(json_node_get_object(root),"runtime");
            auto* runtime=JSON_NODE_HOLDS_OBJECT(runtime_node)?json_node_get_object(runtime_node):nullptr;
            valid=runtime && json_object_has_member(runtime,"library_path") &&
                  json_node_get_node_type(json_object_get_member(runtime,"library_path"))==JSON_NODE_VALUE &&
                  json_node_get_value_type(json_object_get_member(runtime,"library_path"))==G_TYPE_STRING;
            if(valid) {
                fs::path library=json_object_get_string_member(runtime,"library_path");
                valid=!library.empty() && fs::is_regular_file(library.is_absolute()?library:path.parent_path()/library);
            }
        }
    }
    g_clear_error(&error);g_object_unref(parser);
    if(!valid) throw std::runtime_error("Invalid OpenXR runtime manifest");
    auto target=active_runtime();fs::create_directories(target.parent_path());
    if(fs::is_directory(target)) throw std::runtime_error("OpenXR runtime path is a directory");
    auto backup=fs::path(target.string()+".quest-workspace-backup");
    if((fs::exists(target)||fs::is_symlink(target)) && !fs::exists(backup) && !fs::is_symlink(backup))
        fs::copy(target,backup,fs::copy_options::copy_symlinks);
    auto temporary=fs::path(target.string()+".new-"+std::to_string(getpid()));
    fs::remove(temporary);
    fs::create_symlink(fs::absolute(path),temporary);
    try { fs::rename(temporary,target); } catch(...) { fs::remove(temporary);throw; }
}
inline Settings load() {
    Settings result;
    auto path=directory()/"pcvr.json";
    if(!fs::exists(path)) return result;
    JsonParser* parser=json_parser_new();GError* error=nullptr;
    if(!json_parser_load_from_file(parser,path.c_str(),&error)) {
        std::string reason=error?error->message:"Invalid PC VR preferences";
        g_clear_error(&error);g_object_unref(parser);throw std::runtime_error(reason);
    }
    try {
        auto* node=json_parser_get_root(parser);
        if(!JSON_NODE_HOLDS_OBJECT(node)) throw std::runtime_error("Expected PC VR settings object");
        auto* object=json_node_get_object(node);
        if(json_object_has_member(object,"overlay")) {
            auto* value=json_object_get_member(object,"overlay");
            if(json_node_get_value_type(value)!=G_TYPE_BOOLEAN) throw std::runtime_error("Overlay setting must be boolean");
            result.overlay=json_node_get_boolean(value);
        }
        if(json_object_has_member(object,"applications")) {
            auto* value=json_object_get_member(object,"applications");
            if(!JSON_NODE_HOLDS_ARRAY(value)) throw std::runtime_error("Expected startup application list");
            auto* list=json_node_get_array(value);
            if(json_array_get_length(list)>32) throw std::runtime_error("At most32 startup applications are allowed");
            for(guint i=0;i<json_array_get_length(list);++i) {
                auto* item=json_array_get_element(list,i);
                if(!JSON_NODE_HOLDS_OBJECT(item)) throw std::runtime_error("Invalid startup application");
                auto* app=json_node_get_object(item);
                Application entry;
                for(auto* key:{"name","runtime"}) {
                    auto* field=json_object_get_member(app,key);
                    if(!field || json_node_get_value_type(field)!=G_TYPE_STRING) throw std::runtime_error("Invalid application metadata");
                }
                entry.name=json_object_get_string_member(app,"name");
                entry.runtime=json_object_get_string_member(app,"runtime");
                if(entry.runtime!="any" && !runtime_id(entry.runtime)) throw std::runtime_error("Invalid application runtime");
                auto* args=json_object_get_member(app,"argv");
                if(!args || !JSON_NODE_HOLDS_ARRAY(args)) throw std::runtime_error("Application command must be an argument list");
                auto* argv=json_node_get_array(args);
                if(!json_array_get_length(argv)||json_array_get_length(argv)>64) throw std::runtime_error("Invalid command argument count");
                for(guint j=0;j<json_array_get_length(argv);++j) {
                    auto* arg=json_array_get_element(argv,j);
                    if(json_node_get_value_type(arg)!=G_TYPE_STRING) throw std::runtime_error("Application arguments must be strings");
                    std::string part=json_node_get_string(arg);
                    if(part.size()>4096) throw std::runtime_error("Application argument is too long");
                    entry.argv.push_back(part);
                }
                if(entry.name.empty() || entry.name.size()>256 || entry.argv[0].empty()) throw std::runtime_error("Application name/command is empty");
                result.applications.push_back(std::move(entry));
            }
        }
    } catch(...) {g_object_unref(parser);throw;}
    g_object_unref(parser);return result;
}
inline void save(const Settings& settings) {
    JsonBuilder* builder=json_builder_new();json_builder_begin_object(builder);
    json_builder_set_member_name(builder,"overlay");json_builder_add_boolean_value(builder,settings.overlay);
    json_builder_set_member_name(builder,"applications");json_builder_begin_array(builder);
    for(const auto& app:settings.applications) {
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder,"name");json_builder_add_string_value(builder,app.name.c_str());
        json_builder_set_member_name(builder,"runtime");json_builder_add_string_value(builder,app.runtime.c_str());
        json_builder_set_member_name(builder,"argv");json_builder_begin_array(builder);
        for(const auto& arg:app.argv) json_builder_add_string_value(builder,arg.c_str());
        json_builder_end_array(builder);json_builder_end_object(builder);
    }
    json_builder_end_array(builder);json_builder_end_object(builder);
    JsonNode* root=json_builder_get_root(builder);JsonGenerator* generator=json_generator_new();json_generator_set_root(generator,root);
    gchar* data=json_generator_to_data(generator,nullptr);
    fs::create_directories(directory());GError* error=nullptr;
    bool ok=g_file_set_contents_full((directory()/"pcvr.json").c_str(),data,-1,G_FILE_SET_CONTENTS_CONSISTENT,0600,&error);
    g_free(data);g_object_unref(generator);json_node_free(root);g_object_unref(builder);
    if(!ok) {std::string reason=error->message;g_error_free(error);throw std::runtime_error(reason);}
}
inline std::vector<std::string> parse_command(const std::string& command) {
    int argc=0;gchar** argv=nullptr;GError* error=nullptr;
    if(!g_shell_parse_argv(command.c_str(),&argc,&argv,&error)) {
        std::string reason=error->message;g_error_free(error);throw std::runtime_error(reason);
    }
    std::vector<std::string> result;
    for(int i=0;i<argc;++i) result.emplace_back(argv[i]);
    g_strfreev(argv);
    if(result.empty()||result.size()>64||result.front().empty())throw std::runtime_error("Enter a program and at most64 arguments");
    for(const auto& arg:result)if(arg.size()>4096)throw std::runtime_error("Application argument is too long");
    return result;
}
inline std::vector<uint32_t> window_targets(const std::string& text) {
    std::vector<uint32_t> result;
    size_t begin=0;
    while(begin<text.size()) {
        auto end=text.find(',',begin);if(end==std::string::npos)end=text.size();
        auto part=text.substr(begin,end-begin);
        if(result.size()>=4 || part.empty() || part.size()>10 || part.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid PC VR window selection");
        auto id=std::stoull(part);if(id>UINT32_MAX)throw std::runtime_error("Invalid PC VR window ID");
        if(std::find(result.begin(),result.end(),id)==result.end())result.push_back(id);
        begin=end+1;
        if(begin==text.size())throw std::runtime_error("Invalid PC VR window selection");
    }
    if(result.empty())result.push_back(0);
    return result;
}
inline bool mode_active() {
    auto path=directory()/"pcvr-session.lock";
    int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC);
    if(fd<0) return false;
    bool active=flock(fd,LOCK_SH|LOCK_NB)<0;
    if(!active) flock(fd,LOCK_UN);
    ::close(fd);return active;
}
}
