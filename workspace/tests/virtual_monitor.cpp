// SPDX-License-Identifier: MIT
// Run only inside a disposable headless GNOME Shell session.
#include "../host/monitors.hpp"
#include <cassert>
#include <iostream>
#include <thread>
int main() {
    assert(g_getenv("ZENLINK_DISPOSABLE_COMPOSITOR"));gst_init(nullptr,nullptr);
    GError* error=nullptr;Object<GDBusConnection> bus(g_bus_get_sync(G_BUS_TYPE_SESSION,nullptr,&error));assert(bus);
    call(bus.get(),"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","RequestName",g_variant_new("(su)","dev.doromiert.QuestWorkspace.Host",4u));
    for(int i=0;i<100;++i){while(g_main_context_iteration(nullptr,false)){};g_usleep(10000);}
    auto initial=call(bus.get(),"org.gnome.Mutter.DisplayConfig","/org/gnome/Mutter/DisplayConfig","org.gnome.Mutter.DisplayConfig","GetCurrentState");
    Variant before(g_variant_get_child_value(initial.get(),1));auto count=g_variant_n_children(before.get());
    {
        Capture capture(bus.get(),VIRTUAL_BASE,0,true,false,true,"",true,800,600);
        auto session=capture.session,stream=capture.stream;
        for(auto [width,height]:{std::pair{800u,600u},{1200u,800u},{600u,1000u},{1024u,768u}}) {
            if(capture.pipeline)capture.resize_monitor(width,height);
            auto deadline=g_get_monotonic_time()+15000000;bool found=false;
            while(g_get_monotonic_time()<deadline) {
                while(g_main_context_iteration(nullptr,false)){}
                auto pixels=capture.poll();
                if(capture.width==width&&capture.height==height&&!pixels.empty()){found=true;break;}
                g_usleep(1000);
            }
            if(!found)std::cerr<<"Expected "<<width<<'x'<<height<<" got "<<capture.width<<'x'<<capture.height<<" failure="<<capture.failure<<'\n';
            assert(found);assert(capture.session==session&&capture.stream==stream);
            auto state=call(bus.get(),"org.gnome.Mutter.DisplayConfig","/org/gnome/Mutter/DisplayConfig","org.gnome.Mutter.DisplayConfig","GetCurrentState");
            Variant monitors(g_variant_get_child_value(state.get(),1));assert(g_variant_n_children(monitors.get())==count+1);
            bool rate=false;
            for(gsize i=0;i<g_variant_n_children(monitors.get());++i) {
                Variant monitor(g_variant_get_child_value(monitors.get(),i)),modes(g_variant_get_child_value(monitor.get(),1));
                for(gsize j=0;j<g_variant_n_children(modes.get());++j) {
                    Variant mode(g_variant_get_child_value(modes.get(),j)),props(g_variant_get_child_value(mode.get(),6));gboolean current=false;g_variant_lookup(props.get(),"is-current","b",&current);
                    gint w,h;double hz;g_variant_get_child(mode.get(),1,"i",&w);g_variant_get_child(mode.get(),2,"i",&h);g_variant_get_child(mode.get(),3,"d",&hz);
                    if(current&&w==int(width)&&h==int(height)&&hz>119.5&&hz<120.5)rate=true;
                }
            }
            assert(rate);std::cout<<"VIRTUAL_MONITOR_SIZE_PASS "<<width<<'x'<<height<<"@120 same_session same_stream\n";
        }
    }
    for(int i=0;i<50;++i){while(g_main_context_iteration(nullptr,false)){};g_usleep(10000);}
    auto final=call(bus.get(),"org.gnome.Mutter.DisplayConfig","/org/gnome/Mutter/DisplayConfig","org.gnome.Mutter.DisplayConfig","GetCurrentState");
    Variant after(g_variant_get_child_value(final.get(),1));assert(g_variant_n_children(after.get())==count);
    std::cout<<"VIRTUAL_MONITOR_TEST_PASS create independent_resize120Hz same_output cleanup\n";
}
