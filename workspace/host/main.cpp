// SPDX-License-Identifier: MIT
#include "capture.hpp"
#include "pcvr_settings.hpp"

struct Host;
struct Client {
    Host& host;
    Object<SoupWebsocketConnection> ws;
    Object<GDBusConnection> bus;
    Object<GSettings> settings;
    bool authenticated = false, active = true;
    bool interactive = false, hevc = false;
    guint timeout = 0, timer = 0, catalog_timer = 0;
    std::map<uint32_t, uint64_t> windows;
    std::map<uint32_t, workspace::FrameCrop> frame_bounds;
    std::map<uint32_t, std::pair<unsigned,unsigned>> window_limits;
    std::unique_ptr<VirtualDisplay> virtual_display;
    std::set<uint32_t> canvas_windows;
    std::map<uint32_t, std::unique_ptr<Capture>> captures;
    void clear_captures() {
        captures.clear();
        canvas_windows.clear();
        virtual_display.reset();
    }
    void remove_capture(uint32_t id) {
        captures.erase(id);
        canvas_windows.erase(id);
        if (canvas_windows.empty()) virtual_display.reset();
    }
    string last_catalog, last_settings;
    uint32_t focused = UINT32_MAX;
    gint64 heartbeat = g_get_monotonic_time(), rate_start = 0;
    unsigned commands = 0;
    uint64_t bytes_sent = 0, frames_sent = 0;
    Client(Host& owner, SoupWebsocketConnection* connection);
    ~Client();
    void send(JsonNode* node) { string data = dump(node); soup_websocket_connection_send_text(ws.get(), data.c_str()); }
    void error(const string& detail, uint32_t id = UINT32_MAX) {
        Node node = message("error"); json_object_set_string_member(object(node.get()), "message", detail.c_str());
        if (id != UINT32_MAX) json_object_set_int_member(object(node.get()), "id", id);
        send(node.get());
    }
    void catalog();
    void appearance();
    void receive(const char* data, size_t length);
    void touch(JsonObject* object);
    void tick();
    void close(unsigned code, const char* reason) {
        if (soup_websocket_connection_get_state(ws.get()) == SOUP_WEBSOCKET_STATE_OPEN)
            soup_websocket_connection_close(ws.get(), code, reason);
    }
};

struct Host {
    string token, pcvr;
    bool activated=false;
    std::unique_ptr<Client> client;
    GMainLoop* loop = nullptr;
};

Client::Client(Host& owner, SoupWebsocketConnection* connection) : host(owner), ws(SOUP_WEBSOCKET_CONNECTION(g_object_ref(connection))) {
    soup_websocket_connection_set_max_incoming_payload_size(ws.get(), 32768);
    soup_websocket_connection_set_keepalive_interval(ws.get(), 15);
    timeout = g_timeout_add_seconds(5, [](gpointer data) -> gboolean {
        auto& client = *static_cast<Client*>(data); client.timeout = 0;
        if (!client.authenticated) client.close(SOUP_WEBSOCKET_CLOSE_POLICY_VIOLATION, "Pairing required");
        return G_SOURCE_REMOVE;
    }, this);
    g_signal_connect(ws.get(), "message", G_CALLBACK(+[](SoupWebsocketConnection*, SoupWebsocketDataType type, GBytes* payload, gpointer data) {
        auto& client = *static_cast<Client*>(data);
        try {
            if (type != SOUP_WEBSOCKET_DATA_TEXT) throw std::runtime_error("Only JSON commands accepted");
            gsize size = 0; const char* text = static_cast<const char*>(g_bytes_get_data(payload, &size));
            client.receive(text, size);
        } catch (const std::exception& e) {
            if (!client.authenticated) client.close(SOUP_WEBSOCKET_CLOSE_POLICY_VIOLATION, "Pairing rejected");
            else client.error(e.what());
        }
    }), this);
    g_signal_connect(ws.get(), "closed", G_CALLBACK(+[](SoupWebsocketConnection*, gpointer data) {
        auto& client = *static_cast<Client*>(data);
        client.clear_captures();
        g_message("receiver disconnected updates=%" G_GUINT64_FORMAT " bytes=%" G_GUINT64_FORMAT, client.frames_sent, client.bytes_sent);
        g_idle_add(+[](gpointer host) -> gboolean {
            static_cast<Host*>(host)->client.reset(); return G_SOURCE_REMOVE;
        }, &client.host);
    }), this);
    g_signal_connect(ws.get(), "error", G_CALLBACK(+[](SoupWebsocketConnection*, GError* error, gpointer) {
        g_debug("Receiver transport error: %s", error->message);
    }), this);
}
Client::~Client() {
    if (timeout) g_source_remove(timeout);
    if (timer) g_source_remove(timer);
    if (catalog_timer) g_source_remove(catalog_timer);
    clear_captures();
    if (bus) try {
        call(bus.get(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReleaseName",
            g_variant_new("(s)", "dev.doromiert.QuestWorkspace.Host"));
    } catch (...) {}
    if (settings) g_signal_handlers_disconnect_by_data(settings.get(), this);
    g_signal_handlers_disconnect_by_data(ws.get(), this);
    g_message("idle: no receiver, capture, encoding or periodic sources");
}

void Client::appearance() {
    char* accent = g_settings_get_string(settings.get(), "accent-color");
    char* scheme = g_settings_get_string(settings.get(), "color-scheme");
    Node node = message("appearance"); JsonObject* obj = object(node.get());
    json_object_set_string_member(obj, "accent", accent);
    json_object_set_string_member(obj, "scheme", scheme);
    g_free(accent); g_free(scheme);
    string serialized = dump(node.get());
    if (serialized != last_settings) { send(node.get()); last_settings = serialized; }
}

void Client::catalog() {
    Variant reply = call(bus.get(), "org.gnome.Shell", BRIDGE_PATH, BRIDGE, "List");
    const char* text = nullptr; g_variant_get(reply.get(), "(&s)", &text);
    Node list = parse(text);
    if (!JSON_NODE_HOLDS_ARRAY(list.get())) throw std::runtime_error("Invalid extension catalog");
    JsonArray* array = json_node_get_array(list.get());
    std::map<uint32_t, uint64_t> valid;
    for (guint i = 0; i < json_array_get_length(array); ++i) {
        JsonObject* item = object(json_array_get_element(array, i));
        uint32_t id = integer(item, "id", 1, UINT32_MAX);
        valid.emplace(id, std::stoull(str(item, "native_id")));
        if (json_object_has_member(item,"monitor_width"))
            window_limits[id]={unsigned(integer(item,"monitor_width",1,32768)),unsigned(integer(item,"monitor_height",1,32768))};
        if (json_object_has_member(item,"inset_x")) {
            unsigned scale=integer(item,"scale",1,4);
            frame_bounds[id]={unsigned(integer(item,"inset_x",0,8192)),unsigned(integer(item,"inset_y",0,8192)),
                unsigned(integer(item,"width",1,8192))*scale,unsigned(integer(item,"height",1,8192))*scale};
            if (captures.contains(id)) captures.at(id)->update_bounds(frame_bounds.at(id));
        }
    }
    windows = std::move(valid);
    for (auto it = captures.begin(); it != captures.end();) {
        // An empty list also means the shell is locked; stop desktop capture too.
        if (windows.empty() || (it->first && !windows.contains(it->first))) {
            Node node = message("closed"); json_object_set_int_member(object(node.get()), "id", it->first); send(node.get());
            canvas_windows.erase(it->first);
            it = captures.erase(it);
        } else ++it;
    }
    if (canvas_windows.empty()) virtual_display.reset();
    if (last_catalog == text) return;
    last_catalog = text;
    Node node = message("catalog");
    json_object_set_member(object(node.get()), "windows", json_node_copy(list.get())); send(node.get());
}

// Touch slots are stable across all streamed windows, not per pointer/focus.
void Client::touch(JsonObject* obj) {
    JsonNode* member = json_object_get_member(obj,"contacts");
    if (!member || !JSON_NODE_HOLDS_ARRAY(member)) throw std::runtime_error("Expected touch contacts array");
    JsonArray* contacts = json_node_get_array(member);
    unsigned count = json_array_get_length(contacts);
    if (!count || count > 6) throw std::runtime_error("Touch batch must contain1..6 contacts");
    struct Event { uint32_t id; unsigned slot; string phase; double x=0,y=0; };
    std::vector<Event> events;
    std::set<unsigned> seen;
    std::map<unsigned,uint32_t> owners;
    for (const auto& [id,capture] : captures)
        for (unsigned slot : capture->touches) owners.emplace(slot,id);
    // Validate the complete batch before injecting any input.
    for (unsigned i=0;i<count;i++) {
        JsonObject* contact = object(json_array_get_element(contacts,i));
        Event event{uint32_t(integer(contact,"id",0,UINT32_MAX)),unsigned(integer(contact,"slot",0,5)),str(contact,"phase")};
        if (!seen.insert(event.slot).second) throw std::runtime_error("Duplicate touch slot in batch");
        if (event.phase!="down" && event.phase!="motion" && event.phase!="up") throw std::runtime_error("Invalid touch phase");
        if (event.phase!="up") { event.x=number(contact,"x",0,1); event.y=number(contact,"y",0,1); }
        auto owner=owners.find(event.slot);
        if (owner!=owners.end() && owner->second!=event.id) throw std::runtime_error("Touch slot belongs to another window");
        if (event.phase=="down") {
            if (owner!=owners.end()) throw std::runtime_error("Touch slot is already down");
            auto capture=captures.find(event.id);
            if (!active || capture==captures.end() || !capture->second->width || !capture->second->height)
                throw std::runtime_error("Touch target is not streaming");
        }
        events.push_back(event);
    }
    for (const Event& event : events) {
        auto found=captures.find(event.id);
        if (found==captures.end() || !active) continue;
        Capture& capture=*found->second;
        if (event.phase=="down") {
            if (event.id && focused!=event.id) {
                auto reply=call(bus.get(),"org.gnome.Shell",BRIDGE_PATH,BRIDGE,"Focus",g_variant_new("(u)",event.id));
                gboolean success; g_variant_get(reply.get(),"(b)",&success);
                if (!success) throw std::runtime_error("Touch target cannot be focused");
            }
            focused=event.id;
            capture.input("NotifyTouchDown",g_variant_new("(sudd)",capture.stream.c_str(),event.slot,
                capture.crop_x+event.x*(capture.width-1),capture.crop_y+event.y*(capture.height-1)));
            capture.touches.insert(event.slot);
        } else if (capture.touches.contains(event.slot)) {
            if (event.phase=="motion")
                capture.input("NotifyTouchMotion",g_variant_new("(sudd)",capture.stream.c_str(),event.slot,
                    capture.crop_x+event.x*(capture.width-1),capture.crop_y+event.y*(capture.height-1)));
            else { capture.input("NotifyTouchUp",g_variant_new("(u)",event.slot)); capture.touches.erase(event.slot); }
        }
    }
}

void Client::receive(const char* data, size_t length) {
    gint64 now = g_get_monotonic_time();
    if (now - rate_start > 1000000) { commands = 0; rate_start = now; }
    if (++commands > 2000) { close(SOUP_WEBSOCKET_CLOSE_POLICY_VIOLATION, "Command rate exceeded"); return; }
    Node node = parse(data, length); JsonObject* obj = object(node.get());
    string type = str(obj, "type");
    if (!authenticated) {
        if (type != "hello" || integer(obj, "version", 1, 1) != 1 || !constant_equal(str(obj, "token"), host.token))
            throw std::runtime_error("Pairing rejected");
        if (workspace::pcvr::mode_active()) throw std::runtime_error("PC VR is active; desktop windows are composited on the PC");
        if (!gst_is_initialized()) gst_init(nullptr, nullptr);
        GError* error = nullptr; bus.reset(g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error));
        if (!bus) throw std::runtime_error(take_error(error));
        // Ask synchronously so the GNOME bridge observes ownership before List.
        auto ownership = call(bus.get(), "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
            g_variant_new("(su)", "dev.doromiert.QuestWorkspace.Host", 4u));
        guint32 status; g_variant_get(ownership.get(), "(u)", &status);
        if (status != 1 && status != 4) throw std::runtime_error("Another workspace host owns the bridge");
        authenticated = true;
        interactive = json_object_has_member(obj, "interactive_jpeg") && boolean(obj,"interactive_jpeg");
        hevc=json_object_has_member(obj,"hevc") && boolean(obj,"hevc");
        interactive=interactive || hevc;
        if (timeout) { g_source_remove(timeout); timeout = 0; }
        settings.reset(g_settings_new("org.gnome.desktop.interface"));
        g_signal_connect(settings.get(), "changed", G_CALLBACK(+[](GSettings*, const char*, gpointer data) {
            static_cast<Client*>(data)->appearance();
        }), this);
        Node welcome = message("welcome"); JsonObject* info = object(welcome.get());
        json_object_set_int_member(info, "version", 1);
        json_object_set_int_member(info, "max_windows", 4);
        json_object_set_int_member(info, "touch_slots", 6);
        json_object_set_string_member(info, "transport", hevc ? "hevc-hardware-jpeg-still" : interactive ? "jpeg-motion-lossless-still" : "lossless-damage-tiles");
        json_object_set_boolean_member(info, "pcvr", !host.pcvr.empty());
        send(welcome.get()); appearance();
        // Let the shell's NameOwnerChanged callback run before requesting List.
        catalog_timer = g_timeout_add(1000, +[](gpointer data) -> gboolean {
            auto& client = *static_cast<Client*>(data);
            try { client.catalog(); } catch (const std::exception& e) { client.clear_captures(); client.error(e.what()); }
            return G_SOURCE_CONTINUE;
        }, this);
        timer = g_timeout_add(interactive ? 4 : 67, +[](gpointer data) -> gboolean {
            static_cast<Client*>(data)->tick(); return G_SOURCE_CONTINUE;
        }, this);
        g_message("receiver authenticated; capture remains on demand");
        return;
    }
    heartbeat = now;
    if (type == "ping") { Node pong = message("pong"); send(pong.get()); return; }
    if (type == "pause") { active = false; focused = UINT32_MAX; clear_captures(); return; }
    if (type == "resume") { if(workspace::pcvr::mode_active()) throw std::runtime_error("End PC VR before resuming standalone streaming"); active = true; last_catalog.clear(); catalog(); return; }
    if (type == "pcvr") {
        if (host.pcvr.empty()) throw std::runtime_error("PC VR integration is not configured");
        std::string selected;
        for(const auto& [id,capture]:captures) {if(!selected.empty()) selected+=",";selected+=std::to_string(id);}
        active=false;clear_captures();
        bool environment = json_object_has_member(obj, "environment") && boolean(obj, "environment");
        const char* direct[] = {host.pcvr.c_str(), environment ? "environment" : "start", "--windows", selected.c_str(), nullptr};
        const char* managed[] = {"systemd-run","--user","--collect","--unit=zenlink-pcvr-launch","--",host.pcvr.c_str(),environment ? "environment" : "start","--windows",selected.c_str(),nullptr};
        auto argv=host.activated?managed:direct;GError* error = nullptr;
        if (!g_spawn_async(nullptr, const_cast<char**>(argv), nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &error))
            throw std::runtime_error(take_error(error));
        Node response = message("pcvr_started"); json_object_set_string_member(object(response.get()),"runtime",workspace::pcvr::default_runtime().c_str()); send(response.get()); return;
    }
    if (type == "catalog") { last_catalog.clear(); catalog(); return; }
    if (type == "touch") { touch(obj); return; }
    uint32_t id = integer(obj, "id", 0, UINT32_MAX);
    if (type == "select") {
        if (!active || workspace::pcvr::mode_active()) throw std::runtime_error("Standalone receiver is paused during PC VR");
        if (captures.contains(id)) return;
        if (captures.size() >= 4) throw std::runtime_error("At most four windows can be streamed");
        if (id && !windows.contains(id)) throw std::runtime_error("Window no longer exists");
        captures.emplace(id, std::make_unique<Capture>(bus.get(), id, id ? windows.at(id) : 0, interactive, hevc));
        if (frame_bounds.contains(id)) captures.at(id)->update_bounds(frame_bounds.at(id));
        return;
    }
    if (type == "unselect") { remove_capture(id); if (focused == id) focused = UINT32_MAX; return; }
    auto found = captures.find(id);
    if ((type == "ack" || type == "release") && (found == captures.end() || !active)) return;
    if (found == captures.end() || !active) throw std::runtime_error("Window is not being streamed");
    Capture& capture = *found->second;
    if (type == "resize") {
        if (!id) throw std::runtime_error("A monitor is resized through its display settings");
        guint32 width = integer(obj, "width", 320, 4096), height = integer(obj, "height", 240, 4096);
        auto limit=window_limits.contains(id) ? window_limits.at(id) : std::pair<unsigned,unsigned>{1920,1080};
        // Automatic virtual output changes reset some physical HDMI displays.
        if (!canvas_windows.contains(id) && (width+64>limit.first || height+64>limit.second))
            throw std::runtime_error("Resize exceeds the physical capture area; virtual display changes are disabled");
        auto reply = call(bus.get(), "org.gnome.Shell", BRIDGE_PATH, BRIDGE, "Resize", g_variant_new("(uuu)", id, width, height));
        gboolean success; g_variant_get(reply.get(), "(b)", &success);
        if (!success) throw std::runtime_error("This window cannot be resized");
        captures.at(id)->desired_width=width;
        captures.at(id)->desired_height=height;
        return;
    }
    if (type == "ack") {
        auto ack=uint32_t(integer(obj,"sequence",1,UINT32_MAX));
        if (capture.hevc && !capture.video_inflight.empty() && capture.video_inflight.front().first==ack)
            capture.video_inflight.pop_front();
        if (uint32_t(integer(obj, "sequence", 1, UINT32_MAX)) == capture.waiting) capture.waiting = 0;
        return;
    }
    if (type == "release") { capture.release(); return; }
    if (type == "focus") {
        for (auto& [other, entry] : captures) { (void)other; entry->release(); }
        if (id) {
            auto reply = call(bus.get(), "org.gnome.Shell", BRIDGE_PATH, BRIDGE, "Focus", g_variant_new("(u)", id));
            gboolean success; g_variant_get(reply.get(), "(b)", &success);
            if (!success) throw std::runtime_error("Window cannot be focused");
        }
        focused = id; return;
    }
    if (focused != id) throw std::runtime_error("Focus the target before sending input");
    if (type == "pointer") {
        double x = number(obj, "x", 0, 1), y = number(obj, "y", 0, 1);
        if (capture.width && capture.height)
            capture.input("NotifyPointerMotionAbsolute", g_variant_new("(sdd)", capture.stream.c_str(), capture.crop_x + x * (capture.width - 1), capture.crop_y + y * (capture.height - 1)));
    } else if (type == "button") {
        unsigned button = integer(obj, "button", 272, 274); bool down = boolean(obj, "down");
        capture.input("NotifyPointerButton", g_variant_new("(ib)", int(button), down));
        if (down) capture.buttons.insert(button); else capture.buttons.erase(button);
    } else if (type == "key") {
        unsigned key = integer(obj, "keysym", 1, 0x11ffffff); bool down = boolean(obj, "down");
        if (capture.keys.size() >= 32 && !capture.keys.contains(key)) throw std::runtime_error("Too many held keys");
        capture.input("NotifyKeyboardKeysym", g_variant_new("(ub)", key, down));
        if (down) capture.keys.insert(key); else capture.keys.erase(key);
    } else if (type == "scroll") {
        double dx = number(obj, "dx", -100, 100), dy = number(obj, "dy", -100, 100);
        capture.input("NotifyPointerAxis", g_variant_new("(ddu)", dx, dy, 3u));
    } else throw std::runtime_error("Unknown command");
}

void Client::tick() {
    if(active && workspace::pcvr::mode_active()) {
        active=false;clear_captures();close(SOUP_WEBSOCKET_CLOSE_GOING_AWAY,"PC VR owns desktop composition");return;
    }
    if (g_get_monotonic_time() - heartbeat > 30000000) {
        clear_captures(); close(SOUP_WEBSOCKET_CLOSE_GOING_AWAY, "Receiver timed out"); return;
    }
    for (auto it = captures.begin(); it != captures.end();) {
        try {
            Bytes bytes = it->second->poll();
            if (!bytes.empty()) {
                soup_websocket_connection_send_binary(ws.get(), bytes.data(), bytes.size());
                bytes_sent += bytes.size(); ++frames_sent;
                if (it->second->hevc) it->second->video_inflight.emplace_back(it->second->sequence,g_get_monotonic_time());
            }
            auto& capture=*it->second;
            if (capture.awaiting_home && capture.canvas_ready) {
                auto reply=call(bus.get(),"org.gnome.Shell",BRIDGE_PATH,BRIDGE,"Home",g_variant_new("(u)",it->first));
                gboolean success; g_variant_get(reply.get(),"(b)",&success);
                if (!success) throw std::runtime_error("Could not return the window to its physical display");
                // Keep logical layout on the canvas while the compositor
                // shows an interactive scaled preview on the physical display.
                call(bus.get(),"org.gnome.Shell",BRIDGE_PATH,BRIDGE,"Resize",
                    g_variant_new("(uuu)",it->first,capture.desired_width,capture.desired_height));
                capture.awaiting_home=false;
                g_message("oversized capture ready id=%u; interactive preview on physical display",it->first);
            }
            ++it;
        } catch (const std::exception& e) {
            g_warning("capture failed id=%u: %s",it->first,e.what());
            Node notice=message("capture_stopped");
            json_object_set_int_member(object(notice.get()),"id",it->first);
            json_object_set_string_member(object(notice.get()),"message",e.what());
            send(notice.get());
            canvas_windows.erase(it->first); it = captures.erase(it);
        }
    }
    if (canvas_windows.empty()) virtual_display.reset();
}

int main(int argc, char** argv) {
    if (argc == 2 && string(argv[1]) == "--version") { std::puts("quest-workspace-host 0.1.0"); return 0; }
    bool activated=argc==4 && string(argv[3])=="--socket-activated";
    if ((argc!=3 && !activated) || string(argv[1])!="--config") {
        std::fprintf(stderr, "Usage: quest-workspace-host --config /private/path/server.json\n"); return 2;
    }
    try {
        gchar* contents = nullptr; gsize size = 0; GError* error = nullptr;
        if (!g_file_get_contents(argv[2], &contents, &size, &error)) throw std::runtime_error(take_error(error));
        Node config = parse(contents, size); g_free(contents); JsonObject* obj = object(config.get());
        Host host; host.activated=activated; host.token = str(obj, "token");
        if (host.token.size() < 32) throw std::runtime_error("Pairing token must be at least 32 characters");
        if(const char* command=g_getenv("ZENLINK_PCVR_COMMAND"))host.pcvr=command;
        else if (json_object_has_member(obj, "pcvr_command")) host.pcvr = str(obj, "pcvr_command");
        unsigned port = integer(obj, "port", 1024, 65535);
        string cert = str(obj, "certificate"), key = str(obj, "private_key");
        Object<GTlsCertificate> certificate(g_tls_certificate_new_from_files(cert.c_str(), key.c_str(), &error));
        if (!certificate) throw std::runtime_error(take_error(error));
        Object<SoupServer> server(soup_server_new(nullptr, nullptr));
        soup_server_set_tls_certificate(server.get(), certificate.get());
        soup_server_add_websocket_handler(server.get(), "/workspace", nullptr, nullptr,
            +[](SoupServer*, SoupServerMessage* msg, const char*, SoupWebsocketConnection* ws, gpointer data) {
                auto& host = *static_cast<Host*>(data);
                // Browser pages cannot use the privileged paired native receiver channel.
                if (host.client || soup_message_headers_get_one(soup_server_message_get_request_headers(msg), "Origin")) {
                    soup_websocket_connection_close(ws, SOUP_WEBSOCKET_CLOSE_POLICY_VIOLATION, "Native receiver only or host busy"); return;
                }
                host.client = std::make_unique<Client>(host, ws);
            }, &host, nullptr);
        if(activated) {
            const char* pid=g_getenv("LISTEN_PID"),*fds=g_getenv("LISTEN_FDS");
            if(!pid || !fds || std::string(pid)!=std::to_string(getpid()) || std::string(fds)!="1")throw std::runtime_error("Expected one systemd activation socket");
            Object<GSocket> socket(g_socket_new_from_fd(3,&error));
            if(!socket || !soup_server_listen_socket(server.get(),socket.get(),SOUP_SERVER_LISTEN_HTTPS,&error))throw std::runtime_error(take_error(error));
        }else if(!soup_server_listen_all(server.get(),port,SOUP_SERVER_LISTEN_HTTPS,&error))throw std::runtime_error(take_error(error));
        host.loop = g_main_loop_new(nullptr, false);
        auto stop = +[](gpointer data) -> gboolean { g_main_loop_quit(static_cast<Host*>(data)->loop); return G_SOURCE_REMOVE; };
        g_unix_signal_add(SIGTERM, stop, &host); g_unix_signal_add(SIGINT, stop, &host);
        g_message("listening TLS port=%u; idle with no capture, encoding, catalog polling or VR runtime", port);
        if(activated)g_timeout_add_seconds(5,+[](gpointer data)->gboolean {
            auto& host=*static_cast<Host*>(data);if(host.client)return G_SOURCE_CONTINUE;
            g_main_loop_quit(host.loop);return G_SOURCE_REMOVE;
        },&host);
        g_main_loop_run(host.loop);
        host.client.reset(); soup_server_disconnect(server.get()); g_main_loop_unref(host.loop);
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "quest-workspace-host: %s\n", e.what()); return 1; }
}
