// SPDX-License-Identifier: MIT
#include "capture.hpp"
#include <gtk/gtk.h>
#include <iostream>
#include <array>
struct Pattern { std::string connector;std::array<int,64> cells; };
struct Alignment { std::vector<Pattern> patterns; };
static void draw(cairo_t* cr,double width,double height,const Pattern& pattern) {
    cairo_set_source_rgb(cr,1,1,1);cairo_paint(cr);
    double size=0.6*std::min(width,height),cell=size/8,x=(width-size)/2,y=(height-size)/2;
    cairo_set_antialias(cr,CAIRO_ANTIALIAS_NONE);
    for(int row=0;row<8;++row)for(int col=0;col<8;++col) {
        double value=pattern.cells[row*8+col]?1:0;cairo_set_source_rgb(cr,value,value,value);
        cairo_rectangle(cr,x+col*cell,y+row*cell,cell+0.01,cell+0.01);cairo_fill(cr);
    }
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string(argv[1])=="--self-test") {
            Pattern pattern;pattern.cells.fill(1);pattern.cells[0]=0;
            auto* image=cairo_image_surface_create(CAIRO_FORMAT_RGB24,800,600);auto* cr=cairo_create(image);draw(cr,800,600,pattern);cairo_surface_flush(image);
            auto* data=cairo_image_surface_get_data(image);auto stride=cairo_image_surface_get_stride(image);
            if((reinterpret_cast<uint32_t*>(data+135*stride)[230]&0xffffff)!=0 || (reinterpret_cast<uint32_t*>(data)[0]&0xffffff)!=0xffffff)throw std::runtime_error("Marker geometry self-test failed");
            cairo_destroy(cr);cairo_surface_destroy(image);std::cout<<"ALIGNMENT_PATTERN_TEST_PASS centered60percent cells quiet_zone\n";return 0;
        }
        std::string text((std::istreambuf_iterator<char>(std::cin)),{});if(text.size()>32768)throw std::runtime_error("Alignment request is too large");
        Node request=parse(text.c_str());auto* root=object(request.get());auto* member=json_object_get_member(root,"monitors");
        if(!member||!JSON_NODE_HOLDS_ARRAY(member))throw std::runtime_error("Expected alignment monitors");
        auto* monitors=json_node_get_array(member);auto count=json_array_get_length(monitors);if(!count||count>4)throw std::runtime_error("Choose1..4 monitors");
        Alignment state;
        for(guint i=0;i<count;++i) {
            auto* item=object(json_array_get_element(monitors,i));Pattern pattern;pattern.connector=str(item,"connector");
            auto* member=json_object_get_member(item,"cells");if(!member||!JSON_NODE_HOLDS_ARRAY(member)||json_array_get_length(json_node_get_array(member))!=64)throw std::runtime_error("Expected8x8 marker");
            auto* cells=json_node_get_array(member);
            for(unsigned j=0;j<64;++j){auto* value=json_array_get_element(cells,j);if(json_node_get_value_type(value)!=G_TYPE_INT64)throw std::runtime_error("Invalid marker cell");auto cell=json_node_get_int(value);if(cell!=0&&cell!=1)throw std::runtime_error("Invalid marker cell");pattern.cells[j]=cell;}
            state.patterns.push_back(pattern);
        }
        auto* app=gtk_application_new("org.zenos.ZenLink.Alignment",G_APPLICATION_NON_UNIQUE);
        g_signal_connect(app,"activate",G_CALLBACK(+[](GtkApplication* app,gpointer data) {
            auto& state=*static_cast<Alignment*>(data);auto* monitors=gdk_display_get_monitors(gdk_display_get_default());
            for(auto& pattern:state.patterns) {
                GdkMonitor* target=nullptr;
                for(guint i=0;i<g_list_model_get_n_items(monitors);++i){auto* monitor=GDK_MONITOR(g_list_model_get_item(monitors,i));auto* name=gdk_monitor_get_connector(monitor);if(name&&pattern.connector==name){target=monitor;break;}g_object_unref(monitor);}
                if(!target){g_printerr("Monitor disconnected during alignment\n");g_application_quit(G_APPLICATION(app));return;}
                auto* window=GTK_WINDOW(gtk_application_window_new(app));gtk_window_set_title(window,"ZenLink Monitor Alignment");gtk_window_set_decorated(window,false);
                auto* area=gtk_drawing_area_new();gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area),+[](GtkDrawingArea*,cairo_t* cr,int width,int height,gpointer data){draw(cr,width,height,*static_cast<Pattern*>(data));},&pattern,nullptr);gtk_window_set_child(window,area);
                auto* keys=gtk_event_controller_key_new();g_signal_connect(keys,"key-pressed",G_CALLBACK(+[](GtkEventControllerKey*,guint key,guint,GdkModifierType,gpointer app)->gboolean {if(key!=GDK_KEY_Escape)return false;g_application_quit(G_APPLICATION(app));return true;}),app);gtk_widget_add_controller(GTK_WIDGET(window),keys);
                g_signal_connect(window,"close-request",G_CALLBACK(+[](GtkWindow*,gpointer app)->gboolean {g_application_quit(G_APPLICATION(app));return false;}),app);
                gtk_window_fullscreen_on_monitor(window,target);g_object_unref(target);gtk_window_present(window);
            }
            g_timeout_add_seconds(30,+[](gpointer app)->gboolean {g_application_quit(G_APPLICATION(app));return G_SOURCE_REMOVE;},app);
        }),&state);
        int result=g_application_run(G_APPLICATION(app),1,argv);g_object_unref(app);return result;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
