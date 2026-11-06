// SPDX-License-Identifier: MIT
#pragma once
#include <png.h>
#include <turbojpeg.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace workspace {
using Bytes = std::vector<uint8_t>;
inline void u32(Bytes& bytes, uint32_t value) {
    for (unsigned n = 0; n < 4; ++n) bytes.push_back((value >> (n * 8)) & 255);
}

inline Bytes jpeg_frame(uint32_t id, uint32_t sequence, unsigned w, unsigned h, const Bytes& rgb, int quality = 90) {
    if (!w || !h || w > 8192 || h > 8192 || uint64_t(w)*h > 16777216 || rgb.size() != size_t(w)*h*3)
        throw std::runtime_error("Invalid frame dimensions");
    tjhandle encoder = tjInitCompress();
    if (!encoder) throw std::runtime_error("Could not initialize JPEG encoder");
    unsigned char* output = nullptr; unsigned long size = 0;
    int status = tjCompress2(encoder, rgb.data(), w, 0, h, TJPF_RGB, &output, &size, TJSAMP_444, quality, TJFLAG_FASTDCT);
    if (status) {
        std::string detail(tjGetErrorStr2(encoder)); tjFree(output); tjDestroy(encoder); throw std::runtime_error(detail);
    }
    Bytes result{'Q','W','J','1'};
    u32(result,id); u32(result,sequence); u32(result,w); u32(result,h);
    result.insert(result.end(),output,output+size);
    tjFree(output); tjDestroy(encoder); return result;
}

// One reliable, acknowledged message is an atomic delta against the last frame.
class Tiles {
    Bytes previous;
    unsigned width = 0, height = 0;
public:
    void reset() { previous.clear(); }
    unsigned changed_tiles(unsigned w, unsigned h, const Bytes& rgb, unsigned limit) const {
        if (previous.empty() || w != width || h != height || rgb.size() != previous.size()) return limit+1;
        unsigned count = 0;
        for (unsigned y=0; y<h; y+=256) for (unsigned x=0; x<w; x+=256) {
            bool changed=false;
            for (unsigned row=0; !changed && row<std::min(256u,h-y); ++row) {
                size_t offset=(size_t(y+row)*w+x)*3;
                changed=std::memcmp(rgb.data()+offset,previous.data()+offset,std::min(256u,w-x)*3)!=0;
            }
            if (changed && ++count>limit) return count;
        }
        return count;
    }
    Bytes encode(uint32_t id, uint32_t sequence, unsigned w, unsigned h, const Bytes& rgb) {
        if (!w || !h || w > 8192 || h > 8192 || uint64_t(w) * h > 16777216 || rgb.size() != size_t(w) * h * 3)
            throw std::runtime_error("Invalid frame dimensions");
        bool full = previous.empty() || w != width || h != height;
        Bytes result{'Q', 'W', 'S', '1'};
        u32(result, id); u32(result, sequence); u32(result, w); u32(result, h); u32(result, 0);
        uint32_t count = 0;
        for (unsigned y = 0; y < h; y += 256) {
            for (unsigned x = 0; x < w; x += 256) {
                unsigned tw = std::min(256u, w - x), th = std::min(256u, h - y);
                bool changed = full;
                for (unsigned row = 0; !changed && row < th; ++row) {
                    size_t offset = (size_t(y + row) * w + x) * 3;
                    changed = std::memcmp(rgb.data() + offset, previous.data() + offset, tw * 3) != 0;
                }
                if (!changed) continue;
                png_image image{};
                image.version = PNG_IMAGE_VERSION;
                image.width = tw; image.height = th; image.format = PNG_FORMAT_RGB;
                const uint8_t* pixels = rgb.data() + (size_t(y) * w + x) * 3;
                png_alloc_size_t size = 0;
                if (!png_image_write_to_memory(&image, nullptr, &size, false, pixels, w * 3, nullptr))
                    throw std::runtime_error(image.message);
                Bytes png(size);
                if (!png_image_write_to_memory(&image, png.data(), &size, false, pixels, w * 3, nullptr))
                    throw std::runtime_error(image.message);
                u32(result, x); u32(result, y); u32(result, uint32_t(size));
                result.insert(result.end(), png.begin(), png.begin() + size);
                ++count;
            }
        }
        if (!count) return {};
        for (unsigned n = 0; n < 4; ++n) result[20 + n] = (count >> (n * 8)) & 255;
        previous = rgb; width = w; height = h;
        return result;
    }
};
}
