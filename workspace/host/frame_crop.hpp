// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
namespace workspace {
struct FrameCrop {
    unsigned x=0, y=0, width=0, height=0;
    bool operator==(const FrameCrop&) const = default;
    FrameCrop inside(FrameCrop buffer) const {
        if (!width || !height) return buffer;
        unsigned dx=std::min(x,buffer.width), dy=std::min(y,buffer.height);
        unsigned w=std::min(width,buffer.width-dx), h=std::min(height,buffer.height-dy);
        if (!w || !h) return buffer;
        return {buffer.x+dx,buffer.y+dy,w,h};
    }
};
}
