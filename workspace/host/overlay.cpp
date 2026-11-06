// SPDX-License-Identifier: MIT
#include "overlay_gl.hpp"
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openvr.h>
#include "capture.hpp"
#include "pcvr_settings.hpp"
#include "overlay_math.hpp"
#include "overlay_ui.hpp"
#include <signal.h>
#include <array>
#include <fstream>
#include <sstream>
using namespace workspace::overlay;
static volatile sig_atomic_t running=1;
static void xr_check(XrResult result,const char* operation){if(XR_FAILED(result))throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(result));}
#define XR_DO(expression) xr_check((expression),#expression)
struct Controller {Pose aim;bool valid=false,click=false,grab=false;float scroll=0;};
struct Panel {
    uint32_t id;std::unique_ptr<Capture> capture;
    Pose pose;bool positioned=false;
    float metres=1.35f,cursor_u=-1,cursor_v=-1,radius=15;
    unsigned width=0,height=0;
    GLuint source=0,texture=0;unsigned texture_width=0,texture_height=0;
    XrSwapchain swapchain=XR_NULL_HANDLE;std::vector<XrSwapchainImageOpenGLKHR> images;
    vr::VROverlayHandle_t handle=vr::k_ulOverlayHandleInvalid;
    bool dirty=false;
    explicit Panel(uint32_t value):id(value){}
    float physical_height()const{return width?metres*float(height)/width:0.8f;}
};
struct Workspace {
    Graphics& graphics;Object<GDBusConnection> bus;std::vector<std::unique_ptr<Panel>> panels;
    OverlayVisibility visibility;PointerLease pointer;
    std::array<bool,2> clicking{},grabbing{};
    std::array<Panel*,2> pressed{},dragged{};
    std::array<Pose,2> grab_offset{};
    gint64 next_catalog=0;
    std::vector<uint32_t> targets;
    Workspace(Graphics& gl,const std::string& selected):graphics(gl){
        GError* error=nullptr;bus.reset(g_bus_get_sync(G_BUS_TYPE_SESSION,nullptr,&error));if(!bus) throw std::runtime_error(take_error(error));
        bool acquired=false;
        for(int retry=0;retry<30 && running;++retry){
            auto reply=call(bus.get(),"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","RequestName",g_variant_new("(su)","dev.doromiert.QuestWorkspace.Host",4u));
            guint32 status;g_variant_get(reply.get(),"(u)",&status);if(status==1 || status==4){acquired=true;break;}g_usleep(100000);
        }
        if(!acquired) throw std::runtime_error("The standalone workspace still owns capture; close it before entering PC VR");
        targets=workspace::pcvr::window_targets(selected);
        // Let the bridge observe our exclusive bus name before its first call.
        g_usleep(100000);catalog();
    }
    ~Workspace(){
        release();for(auto& p:panels){p->capture.reset();if(p->texture)glDeleteTextures(1,&p->texture);if(p->source)glDeleteTextures(1,&p->source);}
        if(bus)try{call(bus.get(),"org.freedesktop.DBus","/org/freedesktop/DBus","org.freedesktop.DBus","ReleaseName",g_variant_new("(s)","dev.doromiert.QuestWorkspace.Host"));}catch(...){}
    }
    void catalog(){
        auto reply=call(bus.get(),"org.gnome.Shell",BRIDGE_PATH,BRIDGE,"List");const char* text=nullptr;g_variant_get(reply.get(),"(&s)",&text);auto node=parse(text);
        if(!JSON_NODE_HOLDS_ARRAY(node.get()))throw std::runtime_error("Invalid desktop catalog");
        auto* array=json_node_get_array(node.get());std::map<uint32_t,JsonObject*> windows;
        for(guint i=0;i<json_array_get_length(array);++i){auto* item=object(json_array_get_element(array,i));windows.emplace(integer(item,"id",1,UINT32_MAX),item);}
        // An empty catalog also represents the lock screen: release every capture.
        if(windows.empty()){release();for(auto& p:panels)p->capture.reset();return;}
        for(auto id:targets){
            auto found=std::find_if(panels.begin(),panels.end(),[id](const auto& p){return p->id==id;});
            if(id && !windows.contains(id)){if(found!=panels.end()){release();(*found)->capture.reset();}continue;}
            if(found==panels.end()){panels.push_back(std::make_unique<Panel>(id));found=panels.end()-1;}
            auto& p=**found;
            if(!p.capture) p.capture=std::make_unique<Capture>(bus.get(),id,id?std::stoull(str(windows.at(id),"native_id")):0,true,false,true);
            if(id){auto* item=windows.at(id);unsigned scale=integer(item,"scale",1,4);p.radius=15*scale;p.capture->update_bounds({unsigned(integer(item,"inset_x",0,8192)),unsigned(integer(item,"inset_y",0,8192)),unsigned(integer(item,"width",1,8192))*scale,unsigned(integer(item,"height",1,8192))*scale});}
        }
    }
    void poll(){
        while(g_main_context_iteration(nullptr,false)){}
        auto now=g_get_monotonic_time();if(now>=next_catalog){catalog();next_catalog=now+1000000;}
        if(!visibility.visible)return;
        for(auto& p:panels){
            if(!p->capture)continue;
            auto rgb=p->capture->poll();if(rgb.empty())continue;
            auto w=p->capture->width,h=p->capture->height;
            // Each panel keeps its own uncompressed GPU image; no encoder or socket.
            graphics.upload_to(p->source,p->width,p->height,w,h,rgb);
            p->width=w;p->height=h;p->dirty=true;
        }
    }
    void release(){for(auto& p:panels)if(p->capture)p->capture->release();pressed.fill(nullptr);dragged.fill(nullptr);clicking.fill(false);grabbing.fill(false);}
    void visibility_button(bool pressed) {
        if(!visibility.update(pressed))return;
        release();
        for(auto& p:panels)if(p->capture&&p->capture->pipeline)gst_element_set_state(p->capture->pipeline.get(),visibility.visible?GST_STATE_PLAYING:GST_STATE_PAUSED);
    }
    void controls(const std::array<Controller,2>& hands,Pose head){
        if(!visibility.visible){release();return;}
        for(size_t i=0;i<panels.size();++i){auto& p=*panels[i];p.cursor_u=p.cursor_v=-1;if(!p.positioned){p.pose=initial_panel(head,i,panels.size());p.positioned=true;}}
        int owner=pointer.choose(hands[0].valid,hands[1].valid,hands[0].click||hands[0].grab||std::abs(hands[0].scroll)>.2f,hands[1].click||hands[1].grab||std::abs(hands[1].scroll)>.2f);
        for(size_t hand=0;hand<2;++hand){
            auto input=hands[hand];input.valid=input.valid&&int(hand)==owner;Panel* target=nullptr;Hit hit;hit.distance=100;
            if(input.valid)for(auto& p:panels){if(!p->capture||!p->width)continue;auto candidate=intersect(p->pose,p->metres,p->physical_height(),input.aim);if(candidate.valid&&candidate.distance<hit.distance){target=p.get();hit=candidate;}}
            if(!input.valid){if(pressed[hand]&&pressed[hand]->capture){pressed[hand]->capture->input("NotifyPointerButton",g_variant_new("(ib)",272,false));pressed[hand]->capture->buttons.erase(272);}pressed[hand]=dragged[hand]=nullptr;clicking[hand]=grabbing[hand]=false;continue;}
            if(input.grab&&!grabbing[hand]&&target){dragged[hand]=target;grab_offset[hand]=inverse(input.aim)*target->pose;}
            if(input.grab&&dragged[hand])dragged[hand]->pose=input.aim*grab_offset[hand];
            if(!input.grab)dragged[hand]=nullptr;
            grabbing[hand]=input.grab;
            if(target){
                target->cursor_u=hit.u;target->cursor_v=hit.v;
                if(!input.grab && !clicking[hand])target->capture->input("NotifyPointerMotionAbsolute",g_variant_new("(sdd)",target->capture->stream.c_str(),double(hit.u*target->width+target->capture->crop_x),double(hit.v*target->height+target->capture->crop_y)));
                if(input.click&&!clicking[hand]){
                    if(target->id)call(bus.get(),"org.gnome.Shell",BRIDGE_PATH,BRIDGE,"Focus",g_variant_new("(u)",target->id));
                    target->capture->input("NotifyPointerButton",g_variant_new("(ib)",272,true));target->capture->buttons.insert(272);pressed[hand]=target;
                }
                if(std::abs(input.scroll)>0.2f)target->capture->input("NotifyPointerAxis",g_variant_new("(ddu)",0.,double(-input.scroll*12),0u));
            }
            if(!input.click&&clicking[hand]&&pressed[hand]){auto* p=pressed[hand];if(p->capture){p->capture->input("NotifyPointerButton",g_variant_new("(ib)",272,false));p->capture->buttons.erase(272);}pressed[hand]=nullptr;}
            clicking[hand]=input.click;
        }
    }
    void draw(Panel& p,GLuint target){graphics.draw_from(p.source,target,p.width,p.height,p.id?p.radius:0,p.cursor_u,p.cursor_v);p.dirty=false;}
};
static Pose from_xr(XrPosef pose){return{{pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w},{pose.position.x,pose.position.y,pose.position.z}};}
static XrPosef to_xr(Pose pose){return{{pose.rotation.x,pose.rotation.y,pose.rotation.z,pose.rotation.w},{pose.position.x,pose.position.y,pose.position.z}};}
static Pose from_vr(const vr::HmdMatrix34_t& m){
    // Convert the orthonormal tracking matrix without assuming its dominant axis.
    Quat q;float t=m.m[0][0]+m.m[1][1]+m.m[2][2];
    if(t>0){float s=std::sqrt(t+1)*2;q={(m.m[2][1]-m.m[1][2])/s,(m.m[0][2]-m.m[2][0])/s,(m.m[1][0]-m.m[0][1])/s,s/4};}
    else {int i=m.m[1][1]>m.m[0][0]?1:0;if(m.m[2][2]>m.m[i][i])i=2;int j=(i+1)%3,k=(i+2)%3;float s=std::sqrt(1+m.m[i][i]-m.m[j][j]-m.m[k][k])*2;float v[3];v[i]=s/4;v[j]=(m.m[i][j]+m.m[j][i])/s;v[k]=(m.m[i][k]+m.m[k][i])/s;q={v[0],v[1],v[2],(m.m[k][j]-m.m[j][k])/s};}
    return{q,{m.m[0][3],m.m[1][3],m.m[2][3]}};
}
static vr::HmdMatrix34_t to_vr(Pose p){
    auto x=rotate(p.rotation,{1,0,0}),y=rotate(p.rotation,{0,1,0}),z=rotate(p.rotation,{0,0,1});return{{{x.x,y.x,z.x,p.position.x},{x.y,y.y,z.y,p.position.y},{x.z,y.z,z.z,p.position.z}}};
}
#include "overlay_openxr.hpp"
#include "overlay_openvr.hpp"
int main(int argc,char** argv){
    std::string runtime,targets;
    if((argc==3||argc==5)&&std::string(argv[1])=="--runtime")runtime=argv[2];
    if(argc==5){if(std::string(argv[3])!="--windows")return 2;targets=argv[4];}
    if(!workspace::pcvr::runtime_id(runtime)){g_printerr("Usage: quest-workspace-overlay --runtime wivrn|steamvr\n");return 2;}
    signal(SIGINT,[](int){running=0;});signal(SIGTERM,[](int){running=0;});
    try{
        if(!workspace::pcvr::mode_active())throw std::runtime_error("Start a PC VR session before opening its overlay");
        gst_init(nullptr,nullptr);Graphics graphics;
        if(runtime=="wivrn"){OpenXr overlay(graphics);Workspace workspace(graphics,targets);overlay.run(workspace);}
        else{OpenVr overlay;Workspace workspace(graphics,targets);overlay.run(workspace);}
        g_message("PC VR overlay stopped; capture/input released");return 0;
    }catch(const std::exception& e){g_printerr("PC VR overlay: %s\n",e.what());return 1;}
}
