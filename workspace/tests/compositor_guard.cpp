// SPDX-License-Identifier: MIT
#define main compositor_guard_main
#include "../host/compositor_guard.cpp"
#undef main
#include <cassert>
int main(){
    auto root=std::filesystem::temp_directory_path()/("compositor-guard-"+std::to_string(getpid()));std::filesystem::create_directory(root);
    const std::string expected="/nix/store/fixed/lib/libmutter.so";
    assert(check_compositor(root,expected)==1);
    auto fixture=[&](const char* pid,const char* exe,const std::string& library){auto entry=root/pid;std::filesystem::create_directory(entry);std::filesystem::create_symlink(exe,entry/"exe");std::ofstream(entry/"maps")<<"0000-1000 r-xp 0 0:0 0 "<<library<<'\n';};
    fixture("1","/bin/.gnome-shell-calendar-server-wrapped",expected);assert(check_compositor(root,expected)==1);
    fixture("2","/bin/.gnome-shell-wrapped",expected);assert(check_compositor(root,expected)==0);
    fixture("3","/bin/gnome-shell","/nix/store/old/lib/libmutter.so");assert(check_compositor(root,expected)==1);
    std::filesystem::remove_all(root);
    std::cout<<"COMPOSITOR_GUARD_PASS missing compositor Nix wrapped executable library match mixed old session rejection\n";
}
