// SPDX-License-Identifier: MIT
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
static int check_compositor(const std::filesystem::path& proc,const std::string& expected) {
    bool found=false;
    for(const auto& entry:std::filesystem::directory_iterator(proc)) {
        auto id=entry.path().filename().string();if(id.find_first_not_of("0123456789")!=std::string::npos)continue;
        struct stat info{};if(stat(entry.path().c_str(),&info)||info.st_uid!=getuid())continue;
        std::error_code error;auto name=std::filesystem::read_symlink(entry.path()/"exe",error).filename().string();
        if(error||(name!="gnome-shell"&&!name.starts_with(".gnome-shell-wrapped")))continue;
        std::ifstream maps(entry.path()/"maps");std::string line;bool patched=false;
        while(std::getline(maps,line))if(line.ends_with(" "+expected))patched=true;
        if(!patched)return 1;
        found=true;
    }
    return found?0:1;
}
// ExecCondition: capture is enabled only after GNOME maps this verified library.
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        auto expected=std::filesystem::canonical(argv[1]).string();
        int result=check_compositor("/proc",expected);
        if(result)std::cerr<<"Waiting for a GNOME session with the verified Mutter fix\n";
        return result;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
