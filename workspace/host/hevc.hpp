// SPDX-License-Identifier: MIT
#pragma once
#include "tiles.hpp"
#include "stream_settings.hpp"
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#include <deque>

namespace workspace {
// A bounded hardware encoder. No encoder, thread or GPU session exists at idle.
class HevcEncoder {
    GstElement* pipeline=nullptr;
    GstAppSrc* source=nullptr;
    GstAppSink* sink=nullptr;
    GstVideoInfo input{};
    unsigned pending=0, source_width=0,source_height=0;
public:
    HevcEncoder(unsigned width,unsigned height,StreamSettings settings={}) : source_width(width),source_height(height) {
        GError* error=nullptr;
        std::string description="appsrc name=input is-live=true format=time block=false max-buffers=2 "
            "! videoconvert ! video/x-raw,format=NV12 "
            "! vah265enc b-frames=0 ref-frames=1 key-int-max=120 bitrate="+std::to_string(settings.bitrate(width,height))+" aud=true "
            "! h265parse config-interval=-1 ! video/x-h265,stream-format=byte-stream,alignment=au "
            "! appsink name=output sync=false async=false max-buffers=2 drop=false";
        pipeline=gst_parse_launch(description.c_str(),&error);
        if (error || !pipeline) {
            std::string reason=error?error->message:"Could not create HEVC pipeline";
            g_clear_error(&error); if (pipeline) gst_object_unref(pipeline);
            throw std::runtime_error(reason);
        }
        source=GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline),"input"));
        sink=GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline),"output"));
        gst_video_info_set_format(&input,GST_VIDEO_FORMAT_RGB,(width+1)&~1u,(height+1)&~1u);
        input.fps_n=120; input.fps_d=1;
        GstCaps* caps=gst_video_info_to_caps(&input);
        gst_app_src_set_caps(source,caps); gst_caps_unref(caps);
        if (gst_element_set_state(pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE) {
            close(); throw std::runtime_error("Hardware HEVC encoder could not start");
        }
    }
    HevcEncoder(const HevcEncoder&)=delete;
    ~HevcEncoder() { close(); }
    void close() {
        if (pipeline) gst_element_set_state(pipeline,GST_STATE_NULL);
        if (source) gst_object_unref(source);
        if (sink) gst_object_unref(sink);
        if (pipeline) gst_object_unref(pipeline);
        pipeline=nullptr; source=nullptr; sink=nullptr;
    }
    bool available() const { return pending<2; }
    void check() {
        GstBus* bus=gst_element_get_bus(pipeline);
        GstMessage* msg=gst_bus_pop_filtered(bus,GstMessageType(GST_MESSAGE_ERROR|GST_MESSAGE_EOS));
        gst_object_unref(bus);
        if (!msg) return;
        GError* error=nullptr;
        if (GST_MESSAGE_TYPE(msg)==GST_MESSAGE_ERROR) gst_message_parse_error(msg,&error,nullptr);
        std::string reason=error?error->message:"Hardware HEVC encoder stopped";
        g_clear_error(&error); gst_message_unref(msg); throw std::runtime_error(reason);
    }
    bool push(const Bytes& rgb) {
        check(); if (!available()) return false;
        if (rgb.size()!=size_t(source_width)*source_height*3) throw std::runtime_error("HEVC frame size mismatch");
        GstBuffer* buffer=gst_buffer_new_allocate(nullptr,input.size,nullptr);
        GstMapInfo map{};
        if (!gst_buffer_map(buffer,&map,GST_MAP_WRITE)) { gst_buffer_unref(buffer); throw std::runtime_error("HEVC input map failed"); }
        for (unsigned y=0;y<unsigned(input.height);++y) {
            auto* row=map.data+y*input.stride[0];
            const auto* source_row=rgb.data()+size_t(std::min(y,source_height-1))*source_width*3;
            std::memcpy(row,source_row,size_t(source_width)*3);
            if (unsigned(input.width)>source_width) std::memcpy(row+source_width*3,source_row+(source_width-1)*3,3);
        }
        gst_buffer_unmap(buffer,&map);
        // Sparse desktop damage still needs monotonically spaced video timestamps.
        GST_BUFFER_PTS(buffer)=g_get_monotonic_time()*GST_USECOND;
        GST_BUFFER_DURATION(buffer)=GST_SECOND/120;
        if (gst_app_src_push_buffer(source,buffer)!=GST_FLOW_OK) throw std::runtime_error("HEVC input rejected");
        ++pending; return true;
    }
    Bytes poll() {
        check(); GstSample* sample=gst_app_sink_try_pull_sample(sink,0);
        if (!sample) return {};
        GstBuffer* buffer=gst_sample_get_buffer(sample); GstMapInfo map{};
        if (!gst_buffer_map(buffer,&map,GST_MAP_READ)) { gst_sample_unref(sample); throw std::runtime_error("HEVC output map failed"); }
        Bytes bytes(map.data,map.data+map.size);
        gst_buffer_unmap(buffer,&map); gst_sample_unref(sample);
        if (pending) --pending;
        return bytes;
    }
};
}
