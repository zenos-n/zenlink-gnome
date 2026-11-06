// SPDX-License-Identifier: MIT
#pragma once
struct OpenVr {
    std::vector<vr::VROverlayHandle_t> handles;
    OpenVr(){vr::EVRInitError error=vr::VRInitError_None;vr::VR_Init(&error,vr::VRApplication_Overlay);if(error!=vr::VRInitError_None)throw std::runtime_error(std::string("SteamVR overlay initialization: ")+vr::VR_GetVRInitErrorAsEnglishDescription(error));g_message("PCVR_OVERLAY backend=OpenVR single_pcvr_stream=true");}
    ~OpenVr(){for(auto handle:handles)vr::VROverlay()->DestroyOverlay(handle);vr::VR_Shutdown();}
    static void check(vr::EVROverlayError error){if(error!=vr::VROverlayError_None)throw std::runtime_error(vr::VROverlay()->GetOverlayErrorNameFromEnum(error));}
    void run(Workspace& workspace){
        while(running){
            vr::VREvent_t event{};while(vr::VRSystem()->PollNextEvent(&event,sizeof(event)))if(event.eventType==vr::VREvent_Quit)running=0;
            vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0,poses,vr::k_unMaxTrackedDeviceCount);
            workspace.poll();
            bool toggle=false;std::array<Controller,2> controllers{};
            for(unsigned i=0;i<2;++i){auto index=vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(i?vr::TrackedControllerRole_RightHand:vr::TrackedControllerRole_LeftHand);if(index>=vr::k_unMaxTrackedDeviceCount||!poses[index].bPoseIsValid)continue;
                vr::VRControllerState_t state{};if(!vr::VRSystem()->GetControllerState(index,&state,sizeof(state)))continue;if(i==0)toggle=(state.ulButtonPressed&vr::ButtonMaskFromId(vr::k_EButton_ApplicationMenu))!=0;auto& c=controllers[i];c.valid=true;c.aim=from_vr(poses[index].mDeviceToAbsoluteTracking);
                char model[vr::k_unMaxPropertyStringSize];vr::VRSystem()->GetStringTrackedDeviceProperty(index,vr::Prop_RenderModelName_String,model,sizeof(model));
                vr::RenderModel_ControllerMode_State_t mode{};vr::RenderModel_ComponentState_t tip{};
                if(vr::VRRenderModels()->GetComponentState(model,"tip",&state,&mode,&tip))c.aim=c.aim*from_vr(tip.mTrackingToComponentLocal);
                c.click=(state.ulButtonPressed&vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger))!=0;c.grab=(state.ulButtonPressed&vr::ButtonMaskFromId(vr::k_EButton_Grip))!=0;c.scroll=state.rAxis[0].y;
            }
            workspace.visibility_button(toggle);
            if(poses[0].bPoseIsValid)workspace.controls(controllers,from_vr(poses[0].mDeviceToAbsoluteTracking));else workspace.release();
            for(auto& panel:workspace.panels){auto& p=*panel;
                if(!workspace.visibility.visible||!p.capture||!p.width){if(p.handle!=vr::k_ulOverlayHandleInvalid)vr::VROverlay()->HideOverlay(p.handle);continue;}
                if(p.handle==vr::k_ulOverlayHandleInvalid){auto key="dev.doromiert.questworkspace.window."+std::to_string(p.id);check(vr::VROverlay()->CreateOverlay(key.c_str(),"Workspace",&p.handle));handles.push_back(p.handle);check(vr::VROverlay()->SetOverlayFlag(p.handle,vr::VROverlayFlags_VisibleInDashboard,true));}
                // OpenVR retains its compositor texture reference, so replace only on geometry changes.
                if(!p.texture||p.texture_width!=p.width||p.texture_height!=p.height){if(p.texture){vr::VROverlay()->ClearOverlayTexture(p.handle);glDeleteTextures(1,&p.texture);}p.texture=workspace.graphics.texture(p.width,p.height);p.texture_width=p.width;p.texture_height=p.height;}
                workspace.draw(p,p.texture);vr::Texture_t texture{reinterpret_cast<void*>(uintptr_t(p.texture)),vr::TextureType_OpenGL,vr::ColorSpace_Gamma};
                vr::VRTextureBounds_t bounds{0,1,1,0};check(vr::VROverlay()->SetOverlayTextureBounds(p.handle,&bounds));check(vr::VROverlay()->SetOverlayTexture(p.handle,&texture));check(vr::VROverlay()->SetOverlayWidthInMeters(p.handle,p.metres));auto transform=to_vr(p.pose);check(vr::VROverlay()->SetOverlayTransformAbsolute(p.handle,vr::TrackingUniverseStanding,&transform));check(vr::VROverlay()->ShowOverlay(p.handle));
            }
            // SteamVR overlay pacing blocks without an extra render thread or busy poll.
            vr::VROverlay()->WaitFrameSync(100);
        }
        workspace.release();
    }
};
