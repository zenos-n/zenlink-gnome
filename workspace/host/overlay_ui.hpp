// SPDX-License-Identifier: MIT
#pragma once
// Overlay visibility is independent of the immersive app; never pause its session.
struct OverlayVisibility {
    bool visible=true,previous=false;
    bool update(bool pressed){bool changed=pressed&&!previous;if(changed)visible=!visible;previous=pressed;return changed;}
};

struct PointerLease {
    int owner=-1;
    int choose(bool left_valid,bool right_valid,bool left_engaged,bool right_engaged) {
        bool valid[]={left_valid,right_valid},engaged[]={left_engaged,right_engaged};
        if(owner>=0 && valid[owner] && engaged[owner])return owner;
        owner=-1;
        if(right_valid&&right_engaged)owner=1;
        else if(left_valid&&left_engaged)owner=0;
        if(owner>=0)return owner;
        return right_valid?1:left_valid?0:-1;
    }
};
