// SPDX-License-Identifier: MIT
#pragma once
#define GL_GLEXT_PROTOTYPES
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
namespace workspace::overlay {
struct Graphics {
    Display* display=nullptr;GLXFBConfig config{};GLXContext context=nullptr;GLXPbuffer drawable=0;int visual=0;
    GLuint program=0,vao=0,fbo=0,source=0;
    unsigned source_width=0,source_height=0;
    Graphics(){
        display=XOpenDisplay(nullptr);if(!display) throw std::runtime_error("The PC VR overlay needs an Xwayland display for its OpenGL context");
        int attrs[]={GLX_X_RENDERABLE,True,GLX_DRAWABLE_TYPE,GLX_PBUFFER_BIT,GLX_RENDER_TYPE,GLX_RGBA_BIT,GLX_RED_SIZE,8,GLX_GREEN_SIZE,8,GLX_BLUE_SIZE,8,GLX_ALPHA_SIZE,8,None};
        int count=0;auto* configs=glXChooseFBConfig(display,DefaultScreen(display),attrs,&count);
        if(!configs || !count) throw std::runtime_error("No suitable OpenGL framebuffer configuration");
        config=configs[0];XFree(configs);auto* vi=glXGetVisualFromFBConfig(display,config);if(!vi) throw std::runtime_error("No GLX visual");visual=vi->visualid;XFree(vi);
        context=glXCreateNewContext(display,config,GLX_RGBA_TYPE,nullptr,True);
        int size[]={GLX_PBUFFER_WIDTH,16,GLX_PBUFFER_HEIGHT,16,None};drawable=glXCreatePbuffer(display,config,size);
        if(!context || !drawable || !glXMakeContextCurrent(display,drawable,drawable,context)) throw std::runtime_error("Could not create the overlay OpenGL context");
        auto shader=[](GLenum type,const char* text){GLuint value=glCreateShader(type);glShaderSource(value,1,&text,nullptr);glCompileShader(value);GLint ok=0;glGetShaderiv(value,GL_COMPILE_STATUS,&ok);if(!ok){char log[2048];glGetShaderInfoLog(value,sizeof(log),nullptr,log);throw std::runtime_error(log);}return value;};
        auto vs=shader(GL_VERTEX_SHADER,"#version 330\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0,1);}");
        auto fs=shader(GL_FRAGMENT_SHADER,R"GLSL(#version 330
in vec2 uv;out vec4 color;uniform sampler2D image;uniform vec2 size;uniform float radius;uniform vec2 cursor;
void main(){vec2 pixel=uv*size;vec2 q=abs(pixel-size*.5)-(size*.5-vec2(radius));float d=length(max(q,0.))+min(max(q.x,q.y),0.)-radius;float a=1.-smoothstep(-.75,.75,d);vec3 c=texture(image,vec2(uv.x,1.-uv.y)).rgb;float reticle=1.-smoothstep(3.,5.,length((vec2(uv.x,1.-uv.y)-cursor)*size));color=vec4(mix(c,vec3(1),reticle),a);})GLSL");
        program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);glDeleteShader(vs);glDeleteShader(fs);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);if(!linked) throw std::runtime_error("Overlay shader failed to link");
        glGenVertexArrays(1,&vao);glGenFramebuffers(1,&fbo);glGenTextures(1,&source);
    }
    ~Graphics(){if(display){if(context){glDeleteProgram(program);glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&source);glXMakeContextCurrent(display,None,None,nullptr);glXDestroyContext(display,context);}if(drawable)glXDestroyPbuffer(display,drawable);XCloseDisplay(display);}}
    void upload(unsigned width,unsigned height,const std::vector<uint8_t>& rgb){
        if(!width || !height || width>8192 || height>8192 || rgb.size()!=size_t(width)*height*3) throw std::runtime_error("Invalid overlay RGB frame");
        glBindTexture(GL_TEXTURE_2D,source);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        if(width!=source_width||height!=source_height){glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,width,height,0,GL_RGB,GL_UNSIGNED_BYTE,rgb.data());source_width=width;source_height=height;}
        else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,rgb.data());
    }
    void upload_to(GLuint& image,unsigned previous_width,unsigned previous_height,unsigned width,unsigned height,const std::vector<uint8_t>& rgb){
        if(!image)glGenTextures(1,&image);
        auto saved=source;source=image;source_width=previous_width;source_height=previous_height;
        try{upload(width,height,rgb);}catch(...){source=saved;throw;}
        source=saved;
    }
    void draw_from(GLuint image,GLuint target,unsigned width,unsigned height,float radius,float u=-1,float v=-1){
        auto saved=source;source=image;
        try{draw(target,width,height,radius,u,v);}catch(...){source=saved;throw;}
        source=saved;
    }
    GLuint texture(unsigned width,unsigned height){GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);return tex;}
    void draw(GLuint texture,unsigned width,unsigned height,float radius,float u=-1,float v=-1){
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Overlay framebuffer is incomplete");
        glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_FRAMEBUFFER_SRGB);glUseProgram(program);glBindVertexArray(vao);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source);
        glUniform1i(glGetUniformLocation(program,"image"),0);glUniform2f(glGetUniformLocation(program,"size"),width,height);glUniform1f(glGetUniformLocation(program,"radius"),radius);glUniform2f(glGetUniformLocation(program,"cursor"),u,v);glDrawArrays(GL_TRIANGLES,0,3);glBindFramebuffer(GL_FRAMEBUFFER,0);glFlush();
    }
};
}
