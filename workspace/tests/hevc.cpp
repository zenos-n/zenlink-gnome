// SPDX-License-Identifier: MIT
#include "../host/hevc.hpp"
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    gst_init(&argc,&argv);
    std::ofstream output("build/hardware-test.h265",std::ios::binary);
    workspace::HevcEncoder encoder(641,401);
    unsigned count=0; size_t bytes=0;
    for (unsigned n=0;n<12;++n) {
        workspace::Bytes frame(641*401*3,0);
        for (size_t p=0;p<frame.size();p+=3) frame[p+n%3]=255;
        if (!encoder.push(frame)) throw std::runtime_error("Input unexpectedly blocked");
        auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        workspace::Bytes packet;
        while (packet.empty() && std::chrono::steady_clock::now()<end) {
            packet=encoder.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (packet.size()<8) throw std::runtime_error("No hardware HEVC access unit");
        output.write(reinterpret_cast<const char*>(packet.data()),packet.size());
        bytes+=packet.size(); ++count;
    }
    std::cout<<"HEVC_HARDWARE_PASS frames="<<count<<" bytes="<<bytes<<" odd_size=641x401 queue_bound=2\n";
}
