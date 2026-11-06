// SPDX-License-Identifier: MIT
#pragma once
#include "tiles.hpp"
#include "frame_crop.hpp"
#include "hevc.hpp"
#include <gio/gio.h>
#include <glib-unix.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

using workspace::Bytes;
using std::string;
constexpr const char* SC = "org.gnome.Mutter.ScreenCast";
constexpr const char* RD = "org.gnome.Mutter.RemoteDesktop";
constexpr const char* BRIDGE = "dev.doromiert.QuestWorkspace";
constexpr const char* BRIDGE_PATH = "/dev/doromiert/QuestWorkspace";

template<class T> struct Unref { void operator()(T* value) const { if (value) g_object_unref(value); } };
template<class T> using Object = std::unique_ptr<T, Unref<T>>;
struct VariantFree { void operator()(GVariant* value) const { if (value) g_variant_unref(value); } };
using Variant = std::unique_ptr<GVariant, VariantFree>;
struct NodeFree { void operator()(JsonNode* value) const { if (value) json_node_unref(value); } };
using Node = std::unique_ptr<JsonNode, NodeFree>;

inline string take_error(GError* error) {
    string result = error ? error->message : "Unknown platform error";
    g_clear_error(&error); return result;
}
inline Node parse(const char* text, gssize size = -1) {
    Object<JsonParser> parser(json_parser_new()); GError* error = nullptr;
    if (!json_parser_load_from_data(parser.get(), text, size, &error)) throw std::runtime_error(take_error(error));
    return Node(json_node_copy(json_parser_get_root(parser.get())));
}
inline string dump(JsonNode* node) {
    char* data = json_to_string(node, false); string result(data); g_free(data); return result;
}
inline JsonObject* object(JsonNode* node) {
    if (!node || !JSON_NODE_HOLDS_OBJECT(node)) throw std::runtime_error("Expected JSON object");
    return json_node_get_object(node);
}
inline string str(JsonObject* obj, const char* key) {
    JsonNode* node = json_object_get_member(obj, key);
    if (!node || json_node_get_value_type(node) != G_TYPE_STRING) throw std::runtime_error(string("Expected string: ") + key);
    const char* data = json_node_get_string(node);
    if (!data || std::strlen(data) > 8192) throw std::runtime_error("String too long");
    return data;
}
inline int64_t integer(JsonObject* obj, const char* key, int64_t lo, int64_t hi) {
    JsonNode* node = json_object_get_member(obj, key);
    if (!node || (json_node_get_value_type(node) != G_TYPE_INT64 && json_node_get_value_type(node) != G_TYPE_DOUBLE))
        throw std::runtime_error(string("Expected integer: ") + key);
    double value = json_node_get_double(node);
    if (!std::isfinite(value) || value < lo || value > hi || std::trunc(value) != value)
        throw std::runtime_error(string("Out of range: ") + key);
    return int64_t(value);
}
inline double number(JsonObject* obj, const char* key, double lo, double hi) {
    JsonNode* node = json_object_get_member(obj, key);
    if (!node || (json_node_get_value_type(node) != G_TYPE_DOUBLE && json_node_get_value_type(node) != G_TYPE_INT64))
        throw std::runtime_error(string("Expected number: ") + key);
    double value = json_node_get_double(node);
    if (!std::isfinite(value) || value < lo || value > hi) throw std::runtime_error(string("Out of range: ") + key);
    return value;
}
inline bool boolean(JsonObject* obj, const char* key) {
    JsonNode* node = json_object_get_member(obj, key);
    if (!node || json_node_get_value_type(node) != G_TYPE_BOOLEAN) throw std::runtime_error(string("Expected boolean: ") + key);
    return json_node_get_boolean(node);
}
inline Node message(const char* type) {
    Node node(json_node_new(JSON_NODE_OBJECT)); json_node_take_object(node.get(), json_object_new());
    json_object_set_string_member(object(node.get()), "type", type); return node;
}
inline Variant call(GDBusConnection* bus, const char* name, const string& path, const char* iface,
                    const char* method, GVariant* args = nullptr) {
    GError* error = nullptr;
    Variant reply(g_dbus_connection_call_sync(bus, name, path.c_str(), iface, method, args,
        nullptr, G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, &error));
    if (!reply) throw std::runtime_error(string(method) + ": " + take_error(error));
    return reply;
}
inline string path_result(GVariant* value) {
    const char* path = nullptr; g_variant_get(value, "(&o)", &path); return path;
}
inline bool constant_equal(const string& a, const string& b) {
    if (a.size() != b.size()) return false;
    unsigned diff = 0; for (size_t i = 0; i < a.size(); ++i) diff |= a[i] ^ b[i];
    return diff == 0;
}

// Owns only an on-demand virtual output; no monitor pixels are consumed.
struct VirtualDisplay {
    GDBusConnection* bus;
    string session;
    explicit VirtualDisplay(GDBusConnection* connection) : bus(connection) {
        try {
            GVariantBuilder empty; g_variant_builder_init(&empty,G_VARIANT_TYPE_VARDICT);
            session=path_result(call(bus,SC,"/org/gnome/Mutter/ScreenCast",SC,"CreateSession",g_variant_new("(a{sv})",&empty)).get());
            GVariantBuilder mode,modes,properties;
            g_variant_builder_init(&mode,G_VARIANT_TYPE_VARDICT);
            g_variant_builder_add(&mode,"{sv}","size",g_variant_new("(uu)",4352u,4352u));
            g_variant_builder_add(&mode,"{sv}","refresh-rate",g_variant_new_double(60));
            g_variant_builder_add(&mode,"{sv}","is-preferred",g_variant_new_boolean(true));
            g_variant_builder_init(&modes,G_VARIANT_TYPE("aa{sv}"));
            g_variant_builder_add_value(&modes,g_variant_builder_end(&mode));
            g_variant_builder_init(&properties,G_VARIANT_TYPE_VARDICT);
            g_variant_builder_add(&properties,"{sv}","is-platform",g_variant_new_boolean(true));
            g_variant_builder_add(&properties,"{sv}","modes",g_variant_builder_end(&modes));
            call(bus,SC,session,"org.gnome.Mutter.ScreenCast.Session","RecordVirtual",g_variant_new("(a{sv})",&properties));
            call(bus,SC,session,"org.gnome.Mutter.ScreenCast.Session","Start");
            g_message("oversized workspace display created4352x4352; no desktop pixel consumer");
        } catch (...) { stop(); throw; }
    }
    void stop() noexcept {
        if (session.empty()) return;
        try { call(bus,SC,session,"org.gnome.Mutter.ScreenCast.Session","Stop"); } catch (...) {}
        session.clear();
        g_message("oversized workspace display removed");
    }
    ~VirtualDisplay() { stop(); }
};

struct Capture {
    GDBusConnection* bus;
    uint32_t id, sequence = 0, waiting = 0;
    unsigned width = 0, height = 0;
    unsigned crop_x = 0, crop_y = 0;
    bool restore_window = true;
    workspace::FrameCrop frame_bounds;
    GstSample* last_sample = nullptr;
    bool geometry_dirty = false;
    bool awaiting_home = false, canvas_ready = false;
    unsigned desired_width = 0, desired_height = 0;
    bool virtual_output=false,monitor_output=false;
    unsigned mode_width=1920,mode_height=1080;

    void update_bounds(workspace::FrameCrop bounds) {
        if (bounds == frame_bounds) return;
        frame_bounds = bounds;
        geometry_dirty = true;
    }
    string remote, session, stream, failure;
    guint subscription = 0;
    Object<GstElement> pipeline;
    GstAppSink* sink = nullptr;
    workspace::Tiles tiles;
    gint64 sent_at = 0;
    std::set<unsigned> buttons, keys, touches;
    bool interactive = false, needs_lossless = false;
    bool hevc = false, raw = false;
    std::unique_ptr<workspace::HevcEncoder> encoder;
    uint32_t encoder_epoch=0;
    std::deque<std::pair<uint32_t,gint64>> video_inflight;
    unsigned encoder_width=0,encoder_height=0;
    workspace::StreamSettings stream_settings;
    unsigned encoder_quality=UINT_MAX;
    Bytes retained;
    gint64 last_change = 0, last_poll = 0;
    unsigned retained_width = 0, retained_height = 0;

    Capture(GDBusConnection* connection, uint32_t window_id, uint64_t native_id, bool fast = false, bool video = false, bool raw_frames = false,
            std::string connector="",bool virtual_monitor=false,unsigned requested_width=1920,unsigned requested_height=1080) : bus(connection), id(window_id), interactive(fast), hevc(video), raw(raw_frames) {
        virtual_output=virtual_monitor;monitor_output=virtual_monitor||!connector.empty()||!id;
        restore_window=!monitor_output;mode_width=requested_width;mode_height=requested_height;
        try {
            if (!monitor_output) call(bus, "org.gnome.Shell", BRIDGE_PATH, BRIDGE, "Float", g_variant_new("(u)",id));
            call(bus, "org.gnome.Shell", BRIDGE_PATH, BRIDGE, "TouchDevice");
            remote = path_result(call(bus, RD, "/org/gnome/Mutter/RemoteDesktop", RD, "CreateSession").get());
            Variant properties = call(bus, RD, remote, "org.freedesktop.DBus.Properties", "Get",
                g_variant_new("(ss)", "org.gnome.Mutter.RemoteDesktop.Session", "SessionId"));
            Variant child(g_variant_get_child_value(properties.get(), 0));
            Variant session_id(g_variant_get_variant(child.get()));
            GVariantBuilder builder; g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
            g_variant_builder_add(&builder, "{sv}", "remote-desktop-session-id", session_id.get());
            session = path_result(call(bus, SC, "/org/gnome/Mutter/ScreenCast", SC, "CreateSession",
                g_variant_new("(a{sv})", &builder)).get());
            g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
            g_variant_builder_add(&builder, "{sv}", "cursor-mode", g_variant_new_uint32(0));
            if(virtual_output) {
                g_variant_builder_add(&builder,"{sv}","is-platform",g_variant_new_boolean(true));
                // No fixed modes: PipeWire negotiates/resizes this same virtual output.
                stream=path_result(call(bus,SC,session,"org.gnome.Mutter.ScreenCast.Session","RecordVirtual",g_variant_new("(a{sv})",&builder)).get());
            } else if (!monitor_output) {
                g_variant_builder_add(&builder, "{sv}", "window-id", g_variant_new_uint64(native_id));
                stream = path_result(call(bus, SC, session, "org.gnome.Mutter.ScreenCast.Session", "RecordWindow",
                    g_variant_new("(a{sv})", &builder)).get());
            } else {
                stream = path_result(call(bus, SC, session, "org.gnome.Mutter.ScreenCast.Session", "RecordMonitor",
                    g_variant_new("(sa{sv})", connector.c_str(), &builder)).get());
            }
            subscription = g_dbus_connection_signal_subscribe(bus, SC, "org.gnome.Mutter.ScreenCast.Stream",
                "PipeWireStreamAdded", stream.c_str(), nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
                [](GDBusConnection*, const char*, const char*, const char*, const char*, GVariant* args, gpointer data) {
                    auto& capture = *static_cast<Capture*>(data);
                    guint32 node; g_variant_get(args, "(u)", &node);
                    try { capture.start_pipewire(node); } catch (const std::exception& e) { capture.failure = e.what(); }
                }, this, nullptr);
            call(bus, RD, remote, "org.gnome.Mutter.RemoteDesktop.Session", "Start");
        } catch (...) { stop(); throw; }
    }

    void start_pipewire(unsigned node) {
        if (pipeline) return;
        string description = "pipewiresrc path=" + std::to_string(node) +
            " do-timestamp=true";
        if(virtual_output)description+=" ! capsfilter name=virtual_mode caps=video/x-raw,width="+std::to_string(mode_width)+",height="+std::to_string(mode_height)+",framerate=0/1,max-framerate=120/1";
        description+=" ! queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream"
            " ! videoconvert ! video/x-raw,format=RGBx ! appsink name=frames sync=false max-buffers=1 drop=true";
        GError* error = nullptr;
        pipeline.reset(gst_parse_launch(description.c_str(), &error));
        if (error) throw std::runtime_error(take_error(error));
        if (!pipeline) throw std::runtime_error("Could not create capture pipeline");
        GstElement* element = gst_bin_get_by_name(GST_BIN(pipeline.get()), "frames");
        sink = GST_APP_SINK(element); g_object_unref(element);
        if (gst_element_set_state(pipeline.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
            throw std::runtime_error("PipeWire pipeline did not start");
        g_message("capture started id=%u pipewire=%u", id, node);
    }

    void resize_monitor(unsigned width,unsigned height) {
        if(!virtual_output||!pipeline)throw std::runtime_error("Virtual monitor is not ready to resize");
        auto* filter=gst_bin_get_by_name(GST_BIN(pipeline.get()),"virtual_mode");
        if(!filter)throw std::runtime_error("Virtual monitor mode filter is missing");
        auto* caps=gst_caps_new_simple("video/x-raw","width",G_TYPE_INT,int(width),"height",G_TYPE_INT,int(height),
            "framerate",GST_TYPE_FRACTION,0,1,"max-framerate",GST_TYPE_FRACTION,120,1,nullptr);
        g_object_set(filter,"caps",caps,nullptr);gst_caps_unref(caps);gst_object_unref(filter);
        mode_width=width;mode_height=height;geometry_dirty=true;
    }

    void input(const char* method, GVariant* args) {
        call(bus, RD, remote, "org.gnome.Mutter.RemoteDesktop.Session", method, args);
    }
    void release() noexcept {
        for (unsigned button : buttons) try { input("NotifyPointerButton", g_variant_new("(ib)", int(button), false)); } catch (...) {}
        for (unsigned key : keys) try { input("NotifyKeyboardKeysym", g_variant_new("(ub)", key, false)); } catch (...) {}
        for (unsigned slot : touches) try { input("NotifyTouchUp", g_variant_new("(u)", slot)); } catch (...) {}
        buttons.clear(); keys.clear(); touches.clear();
    }
    void stop() noexcept {
        release();
        if (id && restore_window) try { call(bus,"org.gnome.Shell",BRIDGE_PATH,BRIDGE,"Restore",g_variant_new("(u)",id)); } catch (...) {}
        if (subscription) { g_dbus_connection_signal_unsubscribe(bus, subscription); subscription = 0; }
        if (last_sample) { gst_sample_unref(last_sample); last_sample=nullptr; }
        if (pipeline) gst_element_set_state(pipeline.get(), GST_STATE_NULL);
        pipeline.reset(); sink = nullptr;
        if (!remote.empty()) try { call(bus, RD, remote, "org.gnome.Mutter.RemoteDesktop.Session", "Stop"); } catch (...) {}
        remote.clear();
    }
    ~Capture() { stop(); g_message("capture stopped id=%u", id); }

    Bytes poll() {
        if (!failure.empty()) throw std::runtime_error(failure);
        if (!pipeline) return {};
        Object<GstBus> gst_bus(gst_element_get_bus(pipeline.get()));
        if (GstMessage* msg = gst_bus_pop_filtered(gst_bus.get(), GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
            string detail = "Capture ended";
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError* error = nullptr; gst_message_parse_error(msg, &error, nullptr); detail = take_error(error);
            }
            gst_message_unref(msg); throw std::runtime_error(detail);
        }
        if (hevc && !video_inflight.empty()) {
            if (g_get_monotonic_time()-video_inflight.front().second>10000000)
                throw std::runtime_error("Receiver stopped acknowledging HEVC frames");
            if (video_inflight.size()>=stream_settings.inflight()) return {};
        }
        if (hevc && encoder) {
            auto payload=encoder->poll();
            if (!payload.empty()) {
                Bytes packet{'Q','W','H','1'};
                workspace::u32(packet,id); workspace::u32(packet,++sequence);
                workspace::u32(packet,encoder_width); workspace::u32(packet,encoder_height);
                workspace::u32(packet,encoder_epoch);
                packet.insert(packet.end(),payload.begin(),payload.end());
                return packet;
            }
            if (!encoder->available()) return {};
        }
        if (waiting) {
            if (g_get_monotonic_time() - sent_at > 10000000) throw std::runtime_error("Receiver stopped acknowledging frames");
            return {};
        }
        gint64 now = g_get_monotonic_time();
        if (now - last_poll < (interactive ? workspace::StreamSettings::interval_us : 67000)) return {};
        last_poll = now;
        GstSample* sample = gst_app_sink_try_pull_sample(sink, 0);
        if (sample) {
            if (last_sample) gst_sample_unref(last_sample);
            last_sample=gst_sample_ref(sample);
        } else if (geometry_dirty && last_sample) sample=gst_sample_ref(last_sample);
        if (!sample) return settle(now);
        geometry_dirty=false;
        GstVideoInfo info{}; GstVideoFrame frame{};
        if (!gst_video_info_from_caps(&info, gst_sample_get_caps(sample))) {
            gst_sample_unref(sample); throw std::runtime_error("Invalid video caps");
        }
        unsigned buffer_width = GST_VIDEO_INFO_WIDTH(&info), buffer_height = GST_VIDEO_INFO_HEIGHT(&info);
        canvas_ready = buffer_width >= 4352 && buffer_height >= 4352;
        crop_x = 0; crop_y = 0; width = buffer_width; height = buffer_height;
        // Mutter allocates monitor-sized buffers for windows; respect the live crop.
        if (auto* crop = gst_buffer_get_video_crop_meta(gst_sample_get_buffer(sample))) {
            if (crop->width && crop->height && uint64_t(crop->x) + crop->width <= buffer_width &&
                uint64_t(crop->y) + crop->height <= buffer_height) {
                crop_x = crop->x; crop_y = crop->y; width = crop->width; height = crop->height;
            }
        }
        if (!monitor_output) {
            auto content = frame_bounds.inside({crop_x,crop_y,width,height});
            crop_x=content.x; crop_y=content.y; width=content.width; height=content.height;
        }
        if (!width || !height || width > 8192 || height > 8192 || uint64_t(width) * height > 16777216) {
            gst_sample_unref(sample); throw std::runtime_error("Capture exceeds frame limit ("+std::to_string(width)+"x"+std::to_string(height)+")");
        }
        if (!gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
            gst_sample_unref(sample); throw std::runtime_error("Could not map frame");
        }
        Bytes rgb(size_t(width) * height * 3);
        const auto* data = static_cast<uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0));
        const auto stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
        for (unsigned y = 0; y < height; ++y) {
            const auto* row = data + (y + crop_y) * stride + crop_x * 4;
            auto* target = rgb.data() + size_t(y) * width * 3;
            for (unsigned x = 0; x < width; ++x) { target[x*3] = row[x*4]; target[x*3+1] = row[x*4+1]; target[x*3+2] = row[x*4+2]; }
        }
        gst_video_frame_unmap(&frame); gst_sample_unref(sample);
        if (raw) return rgb;
        Bytes bytes;
        if (hevc) {
            if (retained_width == width && retained_height == height && retained == rgb) return settle(now);
            if (!encoder || encoder_width!=width || encoder_height!=height || encoder_quality!=stream_settings.quality) {
                encoder=std::make_unique<workspace::HevcEncoder>(width,height,stream_settings);
                encoder_quality=stream_settings.quality;
                encoder_width=width; encoder_height=height; ++encoder_epoch;
                g_message("HEVC hardware encoder ready id=%u size=%ux%u epoch=%u",id,width,height,encoder_epoch);
            }
            if (encoder->push(rgb)) {
                retained=std::move(rgb); retained_width=width; retained_height=height;
                last_change=now; needs_lossless=true;
            }
        } else if (interactive) {
            if (retained_width == width && retained_height == height && retained == rgb) return settle(now);
            bool small = !needs_lossless && tiles.changed_tiles(width,height,rgb,4)<=4;
            retained = std::move(rgb);
            retained_width = width; retained_height = height;
            last_change = now;
            if (small) bytes = tiles.encode(id,sequence+1,width,height,retained);
            else { needs_lossless = true; bytes = workspace::jpeg_frame(id, sequence + 1, width, height, retained); }
        } else bytes = tiles.encode(id, sequence + 1, width, height, rgb);
        if (!bytes.empty()) { waiting = ++sequence; sent_at = g_get_monotonic_time(); }
        return bytes;
    }

    Bytes settle(gint64 now) {
        if (!interactive || !needs_lossless || retained.empty() || now-last_change < 250000) return {};
        if (hevc) {
            // One full-quality 4:4:4 still after motion stops; never JPEG motion.
            if (now-last_change<1000000) return {};
            auto bytes=workspace::jpeg_frame(id,++sequence,width,height,retained,100);
            needs_lossless=false; return bytes;
        }
        // A JPEG preview changes all pixels; restore a complete lossless image.
        tiles.reset();
        Bytes bytes = tiles.encode(id, sequence+1, width, height, retained);
        needs_lossless = false;
        if (!bytes.empty()) { waiting = ++sequence; sent_at = now; }
        return bytes;
    }
};
