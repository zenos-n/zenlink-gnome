// SPDX-License-Identifier: MIT
#include "../host/overlay_gl.hpp"
#include <cassert>
#include <array>
#include <iostream>
using namespace workspace::overlay;
int main(){
    Graphics gl;const unsigned w=64,h=64;
    std::vector<uint8_t> rgb(w*h*3);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto i=(y*w+x)*3;rgb[i]=y<h/2?255:0;rgb[i+1]=y>=h/2?255:0;rgb[i+2]=x>=w/2?255:0;}
    gl.upload(w,h,rgb);auto texture=gl.texture(w,h);gl.draw(texture,w,h,15);
    glBindFramebuffer(GL_FRAMEBUFFER,gl.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    auto pixel=[](int x,int y){std::array<unsigned char,4> p;glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;};
    auto top=pixel(16,48),bottom=pixel(16,16),corner=pixel(0,0);assert(top[0]>240&&top[1]<10&&top[3]>240);assert(bottom[1]>240&&bottom[0]<10);assert(corner[3]==0);
    glBindFramebuffer(GL_FRAMEBUFFER,0);gl.draw(texture,w,h,15,.25f,.25f);glBindFramebuffer(GL_FRAMEBUFFER,gl.fbo);auto cursor=pixel(16,48);assert(cursor[0]>240&&cursor[1]>240&&cursor[2]>240);
    GLuint first=0,second=0;
    gl.upload_to(first,0,0,w,h,std::vector<uint8_t>(w*h*3,255));
    gl.upload_to(second,0,0,w,h,std::vector<uint8_t>(w*h*3,0));
    assert(first&&second&&first!=second);
    gl.draw_from(first,texture,w,h,0);glBindFramebuffer(GL_FRAMEBUFFER,gl.fbo);assert(pixel(32,32)[0]==255);glBindFramebuffer(GL_FRAMEBUFFER,0);
    gl.draw_from(second,texture,w,h,0);glBindFramebuffer(GL_FRAMEBUFFER,gl.fbo);assert(pixel(32,32)[0]==0);glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDeleteTextures(1,&first);glDeleteTextures(1,&second);
    glDeleteTextures(1,&texture);assert(glGetError()==GL_NO_ERROR);
    std::cout<<"OVERLAY_GL_PASS upright colors rounded alpha reticle\n";
}
