// SPDX-License-Identifier: MIT
#include "../host/overlay_math.hpp"
#include "../host/overlay_ui.hpp"
#include <cassert>
using namespace workspace::overlay;
static bool near(float a,float b){return std::abs(a-b)<0.0001f;}
int main(){
    OverlayVisibility ui;assert(ui.visible);assert(ui.update(true)&&!ui.visible);assert(!ui.update(true)&&!ui.visible);ui.update(false);assert(ui.update(true)&&ui.visible);
    PointerLease pointer;
    assert(pointer.choose(true,true,false,false)==1);
    assert(pointer.choose(true,true,true,false)==0);
    assert(pointer.choose(true,true,true,true)==0);
    assert(pointer.choose(true,true,false,true)==1);
    assert(pointer.choose(true,false,false,true)==0);
    assert(pointer.choose(false,false,false,false)==-1);
    Pose panel{{},{0,0,-2}},hand{};
    auto hit=intersect(panel,2,1,hand);assert(hit.valid&&near(hit.u,.5f)&&near(hit.v,.5f)&&near(hit.distance,2));
    hand.position={.5f,.25f,0};hit=intersect(panel,2,1,hand);assert(hit.valid&&near(hit.u,.75f)&&near(hit.v,.25f));
    hand.position={3,0,0};assert(!intersect(panel,2,1,hand).valid);
    hand.position={0,0,-3};assert(!intersect(panel,2,1,hand).valid);
    Pose grip{{0,std::sin(.35f),0,std::cos(.35f)},{.3f,.7f,-.4f}};
    auto offset=inverse(grip)*panel;auto held=grip*offset;assert(length(held.position-panel.position)<.0001f);
    grip.position=grip.position+Vec{1,2,3};held=grip*offset;assert(length(held.position-(panel.position+Vec{1,2,3}))<.0001f);
    auto centered=initial_panel({},0,1);assert(near(centered.position.x,0)&&near(centered.position.z,-1.6f));
}
