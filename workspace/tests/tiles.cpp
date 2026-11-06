// SPDX-License-Identifier: MIT
#include "../host/tiles.hpp"
#include "../host/frame_crop.hpp"
#include <chrono>
#include <iostream>
#include <jpeglib.h>

static void check(bool ok) { if (!ok) throw std::runtime_error("Tile self-test failed"); }
static uint32_t read(const workspace::Bytes& data, size_t offset) {
    check(offset + 4 <= data.size());
    return data[offset] | uint32_t(data[offset+1])<<8 | uint32_t(data[offset+2])<<16 | uint32_t(data[offset+3])<<24;
}
static void apply_delta(const workspace::Bytes& message, workspace::Bytes& target) {
    check(message.size() >= 24 && read(message, 0) == 0x31535751);
    unsigned w = read(message, 12), h = read(message, 16), count = read(message, 20);
    target.resize(size_t(w)*h*3); size_t cursor = 24;
    for (unsigned n = 0; n < count; ++n) {
        unsigned x=read(message,cursor), y=read(message,cursor+4), size=read(message,cursor+8); cursor+=12;
        check(cursor+size <= message.size()); png_image image{}; image.version=PNG_IMAGE_VERSION;
        check(png_image_begin_read_from_memory(&image, message.data()+cursor, size));
        image.format=PNG_FORMAT_RGB;
        workspace::Bytes pixels(PNG_IMAGE_SIZE(image));
        check(png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr));
        check(x+image.width<=w && y+image.height<=h);
        for (unsigned row=0; row<image.height; ++row)
            std::memcpy(target.data()+((size_t(y)+row)*w+x)*3, pixels.data()+size_t(row)*image.width*3, image.width*3);
        png_image_free(&image); cursor+=size;
    }
    check(cursor==message.size());
}
int main() {
    auto crop=workspace::FrameCrop{24,24,800,600}.inside({0,0,1920,1080});
    check(crop.x==24 && crop.y==24 && crop.width==800 && crop.height==600);
    crop=workspace::FrameCrop{24,24,400,1000}.inside({0,0,1920,1080});
    check(crop.width==400 && crop.height==1000);
    crop=workspace::FrameCrop{24,24,2000,2000}.inside({0,0,1920,1080});
    check(crop.width==1896 && crop.height==1056);
    crop=workspace::FrameCrop{}.inside({5,7,800,600});
    check(crop.x==5 && crop.y==7 && crop.width==800 && crop.height==600);
    workspace::Tiles tiles; workspace::Bytes frame(513*259*3), actual;
    for (size_t n=0;n<frame.size();++n) frame[n]=uint8_t(n*31+n/571);
    auto first=tiles.encode(7,1,513,259,frame); apply_delta(first,actual); check(frame==actual);
    check(read(first,4)==7 && read(first,8)==1 && read(first,20)==6);
    check(tiles.encode(7,2,513,259,frame).empty());
    check(tiles.changed_tiles(513,259,frame,4)==0);
    frame[(258*513+512)*3] ^= 127;
    check(tiles.changed_tiles(513,259,frame,4)==1);
    auto delta=tiles.encode(7,2,513,259,frame); check(read(delta,20)==1); apply_delta(delta,actual); check(frame==actual);
    check(delta.size()<first.size());
    frame.resize(300*200*3); auto resized=tiles.encode(7,3,300,200,frame); apply_delta(resized,actual); check(frame==actual);
    tiles.reset(); check(!tiles.encode(7,4,300,200,frame).empty());
    bool rejected=false; try { tiles.encode(7,5,99999,1,frame); } catch (...) { rejected=true; } check(rejected);
    workspace::Bytes desktop(1920*1080*3, 40); workspace::Tiles benchmark;
    auto begin=std::chrono::steady_clock::now(); auto key=benchmark.encode(1,1,1920,1080,desktop);
    auto key_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    begin=std::chrono::steady_clock::now(); auto jpeg=workspace::jpeg_frame(1,2,1920,1080,desktop);
    auto jpeg_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    check(jpeg.size()>24 && read(jpeg,0)==0x314a5751);
    auto decoder=tjInitDecompress(); int jw=0,jh=0,subsampling=0,colorspace=0;
    check(tjDecompressHeader3(decoder,jpeg.data()+20,jpeg.size()-20,&jw,&jh,&subsampling,&colorspace)==0);
    check(jw==1920 && jh==1080 && subsampling==TJSAMP_444);
    workspace::Bytes decoded(desktop.size());
    check(tjDecompress2(decoder,jpeg.data()+20,jpeg.size()-20,decoded.data(),jw,0,jh,TJPF_RGB,TJFLAG_FASTDCT)==0);
    tjDestroy(decoder);
    auto maximum=workspace::jpeg_frame(1,3,1920,1080,desktop,100);
    jpeg_decompress_struct verifier{}; jpeg_error_mgr error{};
    verifier.err=jpeg_std_error(&error); jpeg_create_decompress(&verifier);
    jpeg_mem_src(&verifier,maximum.data()+20,maximum.size()-20);
    check(jpeg_read_header(&verifier,TRUE)==JPEG_HEADER_OK);
    for(unsigned table=0;table<2;++table) {
        check(verifier.quant_tbl_ptrs[table]!=nullptr);
        for(unsigned n=0;n<64;++n) check(verifier.quant_tbl_ptrs[table]->quantval[n]==1);
    }
    jpeg_destroy_decompress(&verifier);
    for(size_t n=0;n<decoded.size();++n)check(std::abs(int(decoded[n])-int(desktop[n]))<4);
    desktop[100*1920*3+100*3]=255;
    begin=std::chrono::steady_clock::now(); auto patch=benchmark.encode(1,2,1920,1080,desktop);
    auto patch_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    check(benchmark.encode(1,3,1920,1080,desktop).empty());
    std::cout<<"PASS exact reconstruction, odd edges, unchanged suppression, resize, bounds, reset\n"
             <<"1920x1080 synthetic keyframe: "<<key.size()<<" bytes / "<<key_ms<<" ms; one-tile change: "
             <<patch.size()<<" bytes / "<<patch_ms<<" ms; unchanged: 0 bytes\n";
    std::cout<<"Interactive JPEG444 preview: "<<jpeg.size()<<" bytes / "<<jpeg_ms<<" ms\n";
}
