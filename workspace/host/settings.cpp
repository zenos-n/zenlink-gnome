// SPDX-License-Identifier: MIT
#include "pcvr_settings.hpp"
#include "stream_settings.hpp"
#include <adwaita.h>
#include <iostream>
using namespace workspace::pcvr;
struct Preferences {
    Settings settings;
    GtkWindow* window=nullptr;
    AdwPreferencesGroup* apps=nullptr;
    GtkLabel* status=nullptr;
    bool changing=false;
    bool self_test=false;
    void error(const std::string& message) {gtk_label_set_text(status,message.c_str());}
};
static void refresh(Preferences* state);
static void add_dialog(Preferences* state) {
    auto* dialog=ADW_DIALOG(adw_dialog_new());adw_dialog_set_title(dialog,"Add startup application");adw_dialog_set_content_width(dialog,480);
    auto* view=ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(view,adw_header_bar_new());
    auto* page=ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    auto* group=ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_description(group,"The application starts once when your headset connects. Commands are parsed as arguments, without a shell.");
    auto* name=ADW_ENTRY_ROW(adw_entry_row_new());adw_preferences_row_set_title(ADW_PREFERENCES_ROW(name),"Name");
    auto* command=ADW_ENTRY_ROW(adw_entry_row_new());adw_preferences_row_set_title(ADW_PREFERENCES_ROW(command),"Command");
    auto* runtime=ADW_COMBO_ROW(adw_combo_row_new());adw_preferences_row_set_title(ADW_PREFERENCES_ROW(runtime),"Run with");
    const char* names[]={"Any runtime","WiVRn","SteamVR",nullptr};
    auto* model=gtk_string_list_new(names);adw_combo_row_set_model(runtime,G_LIST_MODEL(model));g_object_unref(model);
    auto* preset=gtk_button_new_with_label("Use OVR Advanced Settings");gtk_widget_set_halign(preset,GTK_ALIGN_START);
    g_signal_connect(preset,"clicked",G_CALLBACK(+[](GtkButton* button,gpointer) {
        auto* name=GTK_EDITABLE(g_object_get_data(G_OBJECT(button),"name"));
        auto* command=GTK_EDITABLE(g_object_get_data(G_OBJECT(button),"command"));
        auto* runtime=ADW_COMBO_ROW(g_object_get_data(G_OBJECT(button),"runtime"));
        gtk_editable_set_text(name,"OVR Advanced Settings");gtk_editable_set_text(command,"steam -applaunch 1009850");adw_combo_row_set_selected(runtime,2);
    }),nullptr);
    g_object_set_data(G_OBJECT(preset),"name",name);g_object_set_data(G_OBJECT(preset),"command",command);g_object_set_data(G_OBJECT(preset),"runtime",runtime);
    auto* help=gtk_label_new("OVR Advanced Settings must be installed in Steam and uses SteamVR.");gtk_label_set_wrap(GTK_LABEL(help),true);gtk_widget_add_css_class(help,"dim-label");gtk_label_set_xalign(GTK_LABEL(help),0);
    auto* add=gtk_button_new_with_label("Add application");gtk_widget_add_css_class(add,"suggested-action");gtk_widget_set_halign(add,GTK_ALIGN_END);gtk_widget_set_margin_top(add,12);
    auto* status=gtk_label_new("");gtk_label_set_wrap(GTK_LABEL(status),true);gtk_widget_add_css_class(status,"error");
    for(auto* widget:{GTK_WIDGET(name),GTK_WIDGET(command),GTK_WIDGET(runtime),preset,help,status,add}) adw_preferences_group_add(group,widget);
    adw_preferences_page_add(page,group);adw_toolbar_view_set_content(view,GTK_WIDGET(page));adw_dialog_set_child(dialog,GTK_WIDGET(view));
    struct Form {Preferences* state;AdwDialog* dialog;AdwEntryRow* name;AdwEntryRow* command;AdwComboRow* runtime;GtkLabel* status;};
    auto* form=new Form{state,dialog,name,command,runtime,GTK_LABEL(status)};
    g_signal_connect_data(add,"clicked",G_CALLBACK(+[](GtkButton*,gpointer data) {
        auto& form=*static_cast<Form*>(data);
        try {
            Application app;app.name=gtk_editable_get_text(GTK_EDITABLE(form.name));app.argv=parse_command(gtk_editable_get_text(GTK_EDITABLE(form.command)));
            unsigned selected=adw_combo_row_get_selected(form.runtime);app.runtime=selected==1?"wivrn":selected==2?"steamvr":"any";
            if(app.name.empty() || app.name.size()>256 || app.argv.empty() || app.argv.size()>64) throw std::runtime_error("Enter an application name and command.");
            if(form.state->settings.applications.size()>=32) throw std::runtime_error("At most32 startup applications are allowed.");
            auto next=form.state->settings;next.applications.push_back(std::move(app));save(next);form.state->settings=std::move(next);
            refresh(form.state);adw_dialog_close(form.dialog);
        } catch(const std::exception& e) {gtk_label_set_text(form.status,e.what());}
    }),form,+[](gpointer data,GClosure*){delete static_cast<Form*>(data);},G_CONNECT_DEFAULT);
    adw_dialog_present(dialog,GTK_WIDGET(state->window));
}
static void refresh(Preferences* state) {
    auto* rows=GTK_BOX(g_object_get_data(G_OBJECT(state->apps),"rows"));
    for(GtkWidget* child=gtk_widget_get_first_child(GTK_WIDGET(rows));child;) {
        auto* next=gtk_widget_get_next_sibling(child);gtk_box_remove(rows,child);child=next;
    }
    for(size_t i=0;i<state->settings.applications.size();++i) {
        auto& app=state->settings.applications[i];auto* row=ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row),false);adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),app.name.c_str());
        std::string subtitle=app.runtime=="any"?"Any runtime":app.runtime=="steamvr"?"SteamVR":"WiVRn";
        adw_action_row_set_subtitle(row,subtitle.c_str());
        auto* remove=gtk_button_new_from_icon_name("list-remove-symbolic");gtk_widget_add_css_class(remove,"flat");gtk_widget_set_valign(remove,GTK_ALIGN_CENTER);gtk_widget_set_tooltip_text(remove,"Remove startup application");
        adw_action_row_add_suffix(row,remove);gtk_box_append(rows,GTK_WIDGET(row));
        struct Item {Preferences* state;size_t index;};auto* item=new Item{state,i};
        g_signal_connect_data(remove,"clicked",G_CALLBACK(+[](GtkButton*,gpointer data) {
            auto& item=*static_cast<Item*>(data);
            try {auto next=item.state->settings;next.applications.erase(next.applications.begin()+item.index);save(next);item.state->settings=std::move(next);refresh(item.state);}
            catch(const std::exception& e){item.state->error(e.what());}
        }),item,+[](gpointer data,GClosure*){delete static_cast<Item*>(data);},G_CONNECT_DEFAULT);
    }
}
static void activate(GtkApplication* application,gpointer data) {
    auto* state=static_cast<Preferences*>(data);
    if(state->window) {gtk_window_present(state->window);return;}
    auto* window=ADW_APPLICATION_WINDOW(adw_application_window_new(application));state->window=GTK_WINDOW(window);
    gtk_window_set_title(state->window,"ZenLink VR & Desktop");gtk_window_set_default_size(state->window,600,620);
    auto* view=ADW_TOOLBAR_VIEW(adw_toolbar_view_new());adw_toolbar_view_add_top_bar(view,adw_header_bar_new());
    auto* page=ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    auto* connection_group=ADW_PREFERENCES_GROUP(adw_preferences_group_new());adw_preferences_group_set_title(connection_group,"Headset connection");
    adw_preferences_group_set_description(connection_group,"Pair the Workspace app using USB or an existing wireless debugging connection. USB carries the encrypted stream over the cable; no Wi-Fi is needed.");
    for(bool wired:{true,false}) {
        auto* row=ADW_ACTION_ROW(adw_action_row_new());adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),wired?"USB wired mode":"Wireless debugging");
        adw_action_row_set_subtitle(row,wired?"Connect a USB data cable and allow debugging in the headset.":"Reconnect a Quest already listed in Android debugging.");
        auto* button=gtk_button_new_with_label(wired?"Connect USB":"Reconnect");gtk_widget_set_valign(button,GTK_ALIGN_CENTER);adw_action_row_add_suffix(row,button);
        g_object_set_data(G_OBJECT(button),"wired",GINT_TO_POINTER(wired));
        g_signal_connect(button,"clicked",G_CALLBACK(+[](GtkButton* button,gpointer data) {
            auto* state=static_cast<Preferences*>(data);const char* command=g_getenv("ZENLINK_WORKSPACE_CONNECT");
            if(!command){state->error("Headset connection helper is not installed.");return;}
            GError* error=nullptr;auto* process=g_subprocess_new(GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE),&error,command,g_object_get_data(G_OBJECT(button),"wired")?"--usb":"--adb",nullptr);
            if(!process){state->error(error->message);g_clear_error(&error);return;}
            gtk_widget_set_sensitive(GTK_WIDGET(button),false);state->error("Connecting headset…");
            // Keep the widget alive if the preferences window closes during ADB.
            g_object_set_data_full(G_OBJECT(process),"button",g_object_ref(button),g_object_unref);
            g_object_set_data_full(G_OBJECT(process),"status",g_object_ref(state->status),g_object_unref);
            g_subprocess_communicate_utf8_async(process,nullptr,nullptr,+[](GObject* object,GAsyncResult* result,gpointer) {
                auto* process=G_SUBPROCESS(object);gchar *output=nullptr,*detail=nullptr;GError* error=nullptr;
                bool success=g_subprocess_communicate_utf8_finish(process,result,&output,&detail,&error)&&g_subprocess_get_successful(process);
                gtk_label_set_text(GTK_LABEL(g_object_get_data(object,"status")),success?output:error?error->message:detail);
                gtk_widget_set_sensitive(GTK_WIDGET(g_object_get_data(object,"button")),true);g_free(output);g_free(detail);g_clear_error(&error);g_object_unref(process);
            },nullptr);
        }),state);
        adw_preferences_group_add(connection_group,GTK_WIDGET(row));
    }
    adw_preferences_page_add(page,connection_group);
    auto* streaming=ADW_PREFERENCES_GROUP(adw_preferences_group_new());adw_preferences_group_set_title(streaming,"Streaming");
    adw_preferences_group_set_description(streaming,"HEVC streams are capped at120fps; virtual monitors use120Hz. Responsiveness reduces bitrate and queued frames. Quality gives moving detail more bandwidth. Still images receive a full-quality refresh.");
    auto* quality=gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,0,100,1);gtk_scale_set_draw_value(GTK_SCALE(quality),false);
    gtk_range_set_value(GTK_RANGE(quality),workspace::StreamSettings::load().quality);
    gtk_scale_add_mark(GTK_SCALE(quality),0,GTK_POS_BOTTOM,"Responsiveness");gtk_scale_add_mark(GTK_SCALE(quality),100,GTK_POS_BOTTOM,"Quality");
    gtk_widget_set_margin_bottom(quality,12);gtk_widget_set_tooltip_text(quality,"Quality versus responsiveness");
    g_signal_connect(quality,"value-changed",G_CALLBACK(+[](GtkRange* range,gpointer data) {
        auto* state=static_cast<Preferences*>(data);
        try {workspace::StreamSettings value;value.quality=unsigned(std::lround(gtk_range_get_value(range)));value.save();state->error("");}
        catch(const std::exception& error){state->error(error.what());}
    }),state);
    adw_preferences_group_add(streaming,quality);adw_preferences_page_add(page,streaming);
    auto* runtime_group=ADW_PREFERENCES_GROUP(adw_preferences_group_new());adw_preferences_group_set_title(runtime_group,"OpenXR runtime");
    adw_preferences_group_set_description(runtime_group,"Choose the default runtime for OpenXR applications. Changes apply to newly launched apps.");
    auto* runtime=ADW_COMBO_ROW(adw_combo_row_new());adw_preferences_row_set_title(ADW_PREFERENCES_ROW(runtime),"Default runtime");
    std::string selected=default_runtime();
    const char* choices[]={"WiVRn","SteamVR",nullptr};auto* model=gtk_string_list_new(choices);
    if(selected=="custom") gtk_string_list_append(model,"Custom runtime");
    adw_combo_row_set_model(runtime,G_LIST_MODEL(model));g_object_unref(model);adw_combo_row_set_selected(runtime,selected=="wivrn"?0:selected=="steamvr"?1:2);
    g_object_set_data(G_OBJECT(runtime),"previous",GUINT_TO_POINTER(adw_combo_row_get_selected(runtime)));
    g_signal_connect(runtime,"notify::selected",G_CALLBACK(+[](AdwComboRow* row,GParamSpec*,gpointer data) {
        auto* state=static_cast<Preferences*>(data);if(state->changing) return;
        unsigned current=adw_combo_row_get_selected(row);if(current==2) return;
        try {set_default_runtime(current==0?"wivrn":"steamvr");g_object_set_data(G_OBJECT(row),"previous",GUINT_TO_POINTER(current));state->error("");state->changing=true;auto* model=GTK_STRING_LIST(adw_combo_row_get_model(row));if(g_list_model_get_n_items(G_LIST_MODEL(model))>2)gtk_string_list_remove(model,2);state->changing=false;}
        catch(const std::exception& e) {state->error(e.what());state->changing=true;adw_combo_row_set_selected(row,GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row),"previous")));state->changing=false;}
    }),state);
    adw_preferences_group_add(runtime_group,GTK_WIDGET(runtime));adw_preferences_page_add(page,runtime_group);
    auto* overlay_group=ADW_PREFERENCES_GROUP(adw_preferences_group_new());adw_preferences_group_set_title(overlay_group,"Desktop in PC VR");adw_preferences_group_set_description(overlay_group,"Uses the windows selected in your workspace, or the desktop if none are selected. Left menu toggles visibility; trigger clicks and grip moves windows.");
    auto* overlay=ADW_SWITCH_ROW(adw_switch_row_new());adw_preferences_row_set_title(ADW_PREFERENCES_ROW(overlay),"Show workspace overlay");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(overlay),"Compose desktop windows on the PC inside the existing VR stream.");adw_switch_row_set_active(overlay,state->settings.overlay);
    g_signal_connect(overlay,"notify::active",G_CALLBACK(+[](AdwSwitchRow* row,GParamSpec*,gpointer data) {
        auto* state=static_cast<Preferences*>(data);if(state->changing) return;
        auto next=state->settings;next.overlay=adw_switch_row_get_active(row);
        try {save(next);state->settings=std::move(next);state->error("");}
        catch(const std::exception& e){state->error(e.what());state->changing=true;adw_switch_row_set_active(row,state->settings.overlay);state->changing=false;}
    }),state);
    adw_preferences_group_add(overlay_group,GTK_WIDGET(overlay));adw_preferences_page_add(page,overlay_group);
    state->apps=ADW_PREFERENCES_GROUP(adw_preferences_group_new());adw_preferences_group_set_title(state->apps,"Startup applications");
    adw_preferences_group_set_description(state->apps,"Launch once when a headset enters PC VR. Entries can be limited to WiVRn or SteamVR.");
    auto* add=gtk_button_new_from_icon_name("list-add-symbolic");gtk_widget_set_tooltip_text(add,"Add startup application");gtk_widget_add_css_class(add,"flat");adw_preferences_group_set_header_suffix(state->apps,add);
    g_signal_connect_swapped(add,"clicked",G_CALLBACK(add_dialog),state);
    auto* rows=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_widget_add_css_class(rows,"boxed-list");adw_preferences_group_add(state->apps,rows);g_object_set_data(G_OBJECT(state->apps),"rows",rows);
    adw_preferences_page_add(page,state->apps);
    auto* status_group=ADW_PREFERENCES_GROUP(adw_preferences_group_new());state->status=GTK_LABEL(gtk_label_new(""));gtk_label_set_wrap(state->status,true);gtk_widget_add_css_class(GTK_WIDGET(state->status),"error");adw_preferences_group_add(status_group,GTK_WIDGET(state->status));adw_preferences_page_add(page,status_group);
    refresh(state);adw_toolbar_view_set_content(view,GTK_WIDGET(page));adw_application_window_set_content(window,GTK_WIDGET(view));gtk_window_present(state->window);
    if(state->self_test) g_timeout_add(g_getenv("WORKSPACE_UI_TEST_DELAY_MS")?std::clamp(std::atoi(g_getenv("WORKSPACE_UI_TEST_DELAY_MS")),250,30000):250,+[](gpointer data)->gboolean {
        auto* state=static_cast<Preferences*>(data);std::cout<<"PCVR_SETTINGS_UI_PASS groups runtime overlay startup applications\n";gtk_window_destroy(state->window);return G_SOURCE_REMOVE;
    },state);
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--get-runtime") {std::cout<<default_runtime()<<'\n';return 0;}
        if(argc==3 && std::string(argv[1])=="--runtime-manifest") {auto path=manifest(argv[2]);if(path.empty()) return 1;std::cout<<path.string()<<'\n';return 0;}
        if(argc==2 && std::string(argv[1])=="--pcvr-status") {
            if(!mode_active()) {std::cout<<"stopped\n";return 0;}
            auto state=text(directory()/"pcvr-session.json");
            std::cout<<(state.find("true")!=std::string::npos?"ready":"waiting")<<'\n';return 0;
        }
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    Preferences state;state.self_test=argc==2 && std::string(argv[1])=="--self-test";
    try {state.settings=load();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    auto* app=adw_application_new("org.zenos.ZenLink.Workspace.Preferences",G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app,"activate",G_CALLBACK(activate),&state);int result=g_application_run(G_APPLICATION(app),1,argv);g_object_unref(app);return result;
}
