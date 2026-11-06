// SPDX-License-Identifier: MIT
#include "capture.hpp"
#include <algorithm>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>

static std::string run(const std::vector<std::string>& arguments,const std::string* input=nullptr) {
    const char* adb=g_getenv("ZENLINK_ADB");if(!adb)adb="adb";
    std::vector<const char*> argv{adb};for(const auto& value:arguments)argv.push_back(value.c_str());argv.push_back(nullptr);
    GError* error=nullptr;
    Object<GSubprocess> process(g_subprocess_newv(argv.data(),GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE|G_SUBPROCESS_FLAGS_STDIN_PIPE),&error));
    if(!process)throw std::runtime_error(take_error(error));
    char *output=nullptr,*detail=nullptr;
    if(!g_subprocess_communicate_utf8(process.get(),input?input->c_str():nullptr,nullptr,&output,&detail,&error))throw std::runtime_error(take_error(error));
    std::string result=output?output:"";g_free(output);
    // Never report subprocess input: it contains the private pairing identity.
    bool success=g_subprocess_get_successful(process.get());g_free(detail);
    if(!success)throw std::runtime_error("Quest connection failed. Check USB/wireless debugging authorization and try again.");
    return result;
}
int main(int argc,char** argv) {
    try {
        if(argc!=2 || (std::string(argv[1])!="--usb" && std::string(argv[1])!="--adb"))
            throw std::runtime_error("Usage: zenlink-workspace-connect --usb|--adb");
        bool usb=std::string(argv[1])=="--usb";
        std::string selected;bool selected_usb=false,unauthorized=false;
        std::istringstream devices(run({"devices","-l"}));std::string line;
        while(std::getline(devices,line)) {
            std::istringstream row(line);std::string serial,state;row>>serial>>state;
            bool is_usb=line.find(" usb:")!=std::string::npos;
            if(usb!=is_usb)continue;
            if(state=="unauthorized"){unauthorized=true;continue;}
            if(state!="device")continue;
            auto model=run({"-s",serial,"shell","getprop","ro.product.model"});
            model.erase(std::remove(model.begin(),model.end(),'\r'),model.end());
            model.erase(std::remove(model.begin(),model.end(),'\n'),model.end());
            if(model!="Quest 3" && model!="Quest 3S")continue;
            if(!selected.empty())throw std::runtime_error("More than one Quest is connected. Disconnect the other headset and try again.");
            selected=serial;selected_usb=is_usb;
        }
        if(selected.empty())throw std::runtime_error(unauthorized?"Allow USB debugging in the headset, then try again.":usb?"No USB Quest found. Use a USB data cable, enable developer mode and allow USB debugging.":"No Quest found. Connect USB or wireless debugging first.");
        std::string directory=std::string(g_get_user_config_dir())+"/quest-workspace/";
        std::ifstream file(directory+"server.json");std::string text((std::istreambuf_iterator<char>(file)),{});
        if(!file)throw std::runtime_error("Enable ZenLink VR & Desktop before pairing a headset.");
        auto state=parse(text.c_str());auto* settings=object(state.get());auto port=integer(settings,"port",1024,65535);
        gchar* certificate=nullptr;gsize size=0;GError* error=nullptr;
        if(!g_file_get_contents(str(settings,"certificate").c_str(),&certificate,&size,&error))throw std::runtime_error(take_error(error));
        Node connection=message("connection");auto* obj=object(connection.get());json_object_remove_member(obj,"type");
        json_object_set_string_member(obj,"url","wss://127.0.0.1:9843/workspace");
        json_object_set_string_member(obj,"pcvr_url","wivrn+tcp://127.0.0.1:9757");
        json_object_set_string_member(obj,"hostname","quest-workspace");
        json_object_set_string_member(obj,"token",str(settings,"token").c_str());
        json_object_set_string_member(obj,"certificate",certificate);g_free(certificate);
        json_object_set_string_member(obj,"connection_mode",selected_usb?"usb":"adb-wifi");
        run({"-s",selected,"reverse","tcp:9843","tcp:"+std::to_string(port)});
        run({"-s",selected,"reverse","tcp:9757","tcp:9757"});
        auto private_pair=dump(connection.get());
        run({"-s",selected,"shell","run-as dev.doromiert.questworkspace sh -c 'mkdir -p files && cat > files/connection.json && chmod 600 files/connection.json'"},&private_pair);
        std::cout<<(selected_usb?"USB wired mode is ready.":"Wireless debugging connection is ready.")<<" Open Workspace on the Quest, or press Reconnect if it is already open.\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
