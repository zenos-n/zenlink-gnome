// SPDX-License-Identifier: MIT
#pragma once
#include "capture.hpp"
#include <vector>
constexpr uint32_t MONITOR_BASE=0x70000000u,VIRTUAL_BASE=0x71000000u;
struct Monitor {
    uint32_t id=0;std::string connector,title;
    int x=0,y=0,width=0,height=0,mm_width=0,mm_height=0;double scale=1,refresh=60;
    unsigned transform=0;
};
inline std::vector<Monitor> monitor_list(GDBusConnection* bus) {
    auto state=call(bus,"org.gnome.Mutter.DisplayConfig","/org/gnome/Mutter/DisplayConfig","org.gnome.Mutter.DisplayConfig","GetCurrentState");
    Variant physical(g_variant_get_child_value(state.get(),1)),logical(g_variant_get_child_value(state.get(),2));
    std::vector<Monitor> result;
    for(gsize i=0;i<g_variant_n_children(logical.get());++i) {
        Variant entry(g_variant_get_child_value(logical.get(),i));
        Variant specs(g_variant_get_child_value(entry.get(),5));
        if(!g_variant_n_children(specs.get()))continue;
        Variant spec(g_variant_get_child_value(specs.get(),0)),connector(g_variant_get_child_value(spec.get(),0));
        Monitor monitor;monitor.connector=g_variant_get_string(connector.get(),nullptr);
        if(monitor.connector.starts_with("Meta-"))continue; // Virtual outputs are owned by captures instead.
        g_variant_get_child(entry.get(),0,"i",&monitor.x);g_variant_get_child(entry.get(),1,"i",&monitor.y);
        g_variant_get_child(entry.get(),2,"d",&monitor.scale);g_variant_get_child(entry.get(),3,"u",&monitor.transform);
        for(gsize j=0;j<g_variant_n_children(physical.get());++j) {
            Variant item(g_variant_get_child_value(physical.get(),j)),identity(g_variant_get_child_value(item.get(),0));
            Variant name(g_variant_get_child_value(identity.get(),0));
            if(monitor.connector!=g_variant_get_string(name.get(),nullptr))continue;
            Variant properties(g_variant_get_child_value(item.get(),2));const char* display=nullptr;
            if(g_variant_lookup(properties.get(),"display-name","&s",&display))monitor.title=display;
            g_variant_lookup(properties.get(),"width-mm","i",&monitor.mm_width);g_variant_lookup(properties.get(),"height-mm","i",&monitor.mm_height);
            Variant modes(g_variant_get_child_value(item.get(),1));
            for(gsize k=0;k<g_variant_n_children(modes.get());++k) {
                Variant mode(g_variant_get_child_value(modes.get(),k)),properties(g_variant_get_child_value(mode.get(),6));gboolean current=false;
                if(!g_variant_lookup(properties.get(),"is-current","b",&current)||!current)continue;
                g_variant_get_child(mode.get(),1,"i",&monitor.width);g_variant_get_child(mode.get(),2,"i",&monitor.height);g_variant_get_child(mode.get(),3,"d",&monitor.refresh);
                break;
            }
            break;
        }
        if(!monitor.width||!monitor.height||monitor.scale<=0)continue;
        if(monitor.transform%2){std::swap(monitor.width,monitor.height);std::swap(monitor.mm_width,monitor.mm_height);}
        if(monitor.title.empty())monitor.title=monitor.connector;
        result.push_back(monitor);
    }
    return result;
}
inline Node monitor_json(const Monitor& monitor) {
    Node value(json_node_new(JSON_NODE_OBJECT));auto* obj=json_object_new();json_node_take_object(value.get(),obj);
    json_object_set_int_member(obj,"id",monitor.id);json_object_set_string_member(obj,"connector",monitor.connector.c_str());
    json_object_set_string_member(obj,"title",monitor.title.c_str());json_object_set_string_member(obj,"app","Monitor");
    json_object_set_boolean_member(obj,"monitor",true);json_object_set_int_member(obj,"width",monitor.width);json_object_set_int_member(obj,"height",monitor.height);
    json_object_set_int_member(obj,"x",monitor.x);json_object_set_int_member(obj,"y",monitor.y);
    json_object_set_int_member(obj,"scale",1);json_object_set_double_member(obj,"display_scale",monitor.scale);json_object_set_double_member(obj,"refresh",monitor.refresh);
    json_object_set_double_member(obj,"physical_width",monitor.mm_width/1000.0);json_object_set_double_member(obj,"physical_height",monitor.mm_height/1000.0);
    return value;
}
