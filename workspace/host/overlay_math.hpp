// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <algorithm>
namespace workspace::overlay {
struct Vec {float x=0,y=0,z=0; Vec operator+(Vec b)const{return{x+b.x,y+b.y,z+b.z};} Vec operator-(Vec b)const{return{x-b.x,y-b.y,z-b.z};} Vec operator*(float k)const{return{x*k,y*k,z*k};}};
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float length(Vec a){return std::sqrt(dot(a,a));}
struct Quat {float x=0,y=0,z=0,w=1;};
inline Quat inverse(Quat q){return{-q.x,-q.y,-q.z,q.w};}
inline Quat operator*(Quat a,Quat b){return{a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
inline Vec rotate(Quat q,Vec p){auto r=q*Quat{p.x,p.y,p.z,0}*inverse(q);return{r.x,r.y,r.z};}
struct Pose {Quat rotation;Vec position;};
inline Pose operator*(Pose a,Pose b){return{a.rotation*b.rotation,a.position+rotate(a.rotation,b.position)};}
inline Pose inverse(Pose a){auto q=inverse(a.rotation);return{q,rotate(q,a.position*-1)};}
struct Hit {bool valid=false;float u=0,v=0,distance=0;};
inline Hit intersect(Pose panel,float width,float height,Pose aim){
    auto local=inverse(panel)*aim;auto direction=rotate(local.rotation,{0,0,-1});
    if(local.position.z<=0 || direction.z>=-0.0001f || width<=0 || height<=0) return {};
    float distance=-local.position.z/direction.z;auto p=local.position+direction*distance;
    float u=p.x/width+0.5f,v=0.5f-p.y/height;
    return {u>=0 && u<=1 && v>=0 && v<=1,u,v,distance};
}
inline Pose initial_panel(Pose head,unsigned index,unsigned count){
    auto forward=rotate(head.rotation,{0,0,-1});float yaw=std::atan2(-forward.x,-forward.z);
    Quat facing{0,std::sin(yaw/2),0,std::cos(yaw/2)};
    return {facing,head.position+rotate(facing,{(float(index)-float(count-1)/2)*1.45f,-0.15f,-1.6f})};
}
}
