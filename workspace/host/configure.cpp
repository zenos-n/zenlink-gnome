// SPDX-License-Identifier: MIT
#include "pcvr_settings.hpp"
#include <sys/random.h>
#include <sys/stat.h>
#include <cerrno>
#include <iostream>
namespace fs=std::filesystem;
static void write(JsonObject* object,const fs::path& path) {
    JsonNode* node=json_node_new(JSON_NODE_OBJECT);json_node_set_object(node,object);char* data=json_to_string(node,true);GError* error=nullptr;
    bool ok=g_file_set_contents_full(path.c_str(),data,-1,G_FILE_SET_CONTENTS_CONSISTENT,0600,&error);g_free(data);json_node_free(node);
    if(!ok){std::string reason=error->message;g_error_free(error);throw std::runtime_error(reason);}
}
int main(int argc,char** argv) {
    try {
        if(argc<2)throw std::runtime_error("Usage: zenlink-workspace-configure --prepare|--port|--wivrn MODE");
        fs::path directory=workspace::pcvr::directory();fs::create_directories(directory);chmod(directory.c_str(),0700);
        std::string mode=argv[1];auto file=directory/"server.json";
        if(mode=="--wivrn") {
            if(argc!=3)throw std::runtime_error("Choose start, pair or environment");std::string kind=argv[2];
            if(kind!="start"&&kind!="pair"&&kind!="environment")throw std::runtime_error("Invalid PC VR mode");
            JsonObject* config=json_object_new();json_object_set_null_member(config,"openvr-compat-path");json_object_set_null_member(config,"publish-service");json_object_set_boolean_member(config,"tcp-only",true);json_object_set_string_member(config,"hostname","ZenLink PC VR");
            if(kind=="environment") {const char* runner=g_getenv("ZENLINK_PC_ENVIRONMENT_COMMAND");if(!runner)throw std::runtime_error("PC environment runner is not installed");JsonArray* args=json_array_new();json_array_add_string_element(args,runner);json_object_set_array_member(config,"application",args);}
            auto path=directory/"wivrn-session.json";write(config,path);json_object_unref(config);std::cout<<path.string()<<'\n';return 0;
        }
        if(!fs::exists(file)) {
            if(mode!="--prepare")throw std::runtime_error("Set up the workspace before connecting");
            auto key=directory/"server.key",certificate=directory/"server.crt";
            if(!fs::exists(key)||!fs::exists(certificate)) {
                const char* executable=g_getenv("ZENLINK_OPENSSL");if(!executable)executable="openssl";
                std::vector<std::string> command={executable,"req","-x509","-newkey","ec","-pkeyopt","ec_paramgen_curve:P-256","-nodes","-keyout",key.string(),"-out",certificate.string(),"-days","3650","-subj","/CN=quest-workspace","-addext","subjectAltName=DNS:quest-workspace"};std::vector<char*> args;for(auto& arg:command)args.push_back(arg.data());args.push_back(nullptr);
                GError* error=nullptr;int status=0;umask(0077);
                bool ok=g_spawn_sync(nullptr,args.data(),nullptr,GSpawnFlags(G_SPAWN_SEARCH_PATH|G_SPAWN_STDOUT_TO_DEV_NULL|G_SPAWN_STDERR_TO_DEV_NULL),nullptr,nullptr,nullptr,nullptr,&status,&error);
                if(!ok||!g_spawn_check_wait_status(status,&error)){std::string reason=error?error->message:"Certificate generation failed";g_clear_error(&error);throw std::runtime_error(reason);}
            }
            unsigned char random[32];size_t offset=0;
            while(offset<sizeof(random)){auto n=getrandom(random+offset,sizeof(random)-offset,0);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("Could not create a pairing secret");offset+=n;}
            const char* digits="0123456789abcdef";std::string token;for(auto value:random){token+=digits[value>>4];token+=digits[value&15];}
            JsonObject* config=json_object_new();json_object_set_string_member(config,"token",token.c_str());json_object_set_int_member(config,"port",9843);json_object_set_string_member(config,"certificate",certificate.c_str());json_object_set_string_member(config,"private_key",key.c_str());write(config,file);json_object_unref(config);
        }
        JsonParser* parser=json_parser_new();GError* error=nullptr;
        if(!json_parser_load_from_file(parser,file.c_str(),&error)){std::string reason=error->message;g_error_free(error);g_object_unref(parser);throw std::runtime_error(reason);}
        auto* node=json_parser_get_root(parser);if(!JSON_NODE_HOLDS_OBJECT(node))throw std::runtime_error("Invalid workspace configuration");auto* config=json_node_get_object(node);
        auto* port=json_object_get_member(config,"port");if(!port||json_node_get_value_type(port)!=G_TYPE_INT64)throw std::runtime_error("Invalid workspace listener port");auto number=json_node_get_int(port);if(number<1024||number>65535)throw std::runtime_error("Invalid workspace listener port");
        if(mode=="--prepare"&&!fs::exists(directory/"connection.json")) {
            for(auto* key:{"token","certificate"}) {auto* value=json_object_get_member(config,key);if(!value||json_node_get_value_type(value)!=G_TYPE_STRING)throw std::runtime_error("Invalid workspace pairing configuration");}
            auto certificate=workspace::pcvr::text(json_object_get_string_member(config,"certificate"));if(certificate.empty())throw std::runtime_error("Workspace certificate is missing");
            JsonObject* connection=json_object_new();std::string url="wss://127.0.0.1:"+std::to_string(number)+"/workspace";
            json_object_set_string_member(connection,"url",url.c_str());json_object_set_string_member(connection,"token",json_object_get_string_member(config,"token"));json_object_set_string_member(connection,"certificate",certificate.c_str());json_object_set_string_member(connection,"hostname","quest-workspace");json_object_set_string_member(connection,"pcvr_url","wivrn+tcp://127.0.0.1:9757");write(connection,directory/"connection.json");json_object_unref(connection);
        }
        if(mode=="--port")std::cout<<number<<'\n';
        else if(mode!="--prepare")throw std::runtime_error("Unknown workspace configuration operation");
        g_object_unref(parser);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
