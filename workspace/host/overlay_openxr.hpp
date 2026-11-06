// SPDX-License-Identifier: MIT
#pragma once
struct OpenXr {
    Graphics& graphics;
    XrInstance instance=XR_NULL_HANDLE;XrSystemId system=0;XrSession session=XR_NULL_HANDLE;
    XrSpace local=XR_NULL_HANDLE,view=XR_NULL_HANDLE;XrActionSet actions=XR_NULL_HANDLE;
    XrAction aim=XR_NULL_HANDLE,click=XR_NULL_HANDLE,grab=XR_NULL_HANDLE,scroll=XR_NULL_HANDLE,toggle=XR_NULL_HANDLE;
    std::array<XrPath,2> paths{};std::array<XrSpace,2> hand_spaces{};
    std::vector<XrSwapchain> chains;std::map<XrSwapchain,std::pair<unsigned,unsigned>> sizes;
    bool begun=false;int64_t format=GL_RGBA8;
    XrPath path(const char* text){XrPath value;XR_DO(xrStringToPath(instance,text,&value));return value;}
    explicit OpenXr(Graphics& gl):graphics(gl){
        try {initialize();}catch(...){destroy();throw;}
    }
    void initialize(){
        uint32_t count=0;XR_DO(xrEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr));
        std::vector<XrExtensionProperties> extensions(count,{XR_TYPE_EXTENSION_PROPERTIES});XR_DO(xrEnumerateInstanceExtensionProperties(nullptr,count,&count,extensions.data()));
        const char* wanted[]={XR_KHR_OPENGL_ENABLE_EXTENSION_NAME,XR_EXTX_OVERLAY_EXTENSION_NAME};
        for(auto* name:wanted)if(std::none_of(extensions.begin(),extensions.end(),[name](auto& extension){return std::string(extension.extensionName)==name;}))throw std::runtime_error(std::string("Selected runtime cannot provide the workspace overlay: missing ")+name);
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};std::snprintf(info.applicationInfo.applicationName,XR_MAX_APPLICATION_NAME_SIZE,"Quest Workspace Overlay");info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);info.enabledExtensionCount=2;info.enabledExtensionNames=wanted;XR_DO(xrCreateInstance(&info,&instance));
        XrSystemGetInfo get{XR_TYPE_SYSTEM_GET_INFO};get.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;XR_DO(xrGetSystem(instance,&get,&system));
        PFN_xrGetOpenGLGraphicsRequirementsKHR requirements_fn=nullptr;XR_DO(xrGetInstanceProcAddr(instance,"xrGetOpenGLGraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&requirements_fn)));
        XrGraphicsRequirementsOpenGLKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};XR_DO(requirements_fn(instance,system,&requirements));
        GLint major=0,minor=0;glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
        auto version=XR_MAKE_VERSION(major,minor,0);if(version<requirements.minApiVersionSupported||version>requirements.maxApiVersionSupported)throw std::runtime_error("Overlay OpenGL version is outside the runtime's supported range");
        XrSessionCreateInfoOverlayEXTX overlay{XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX};overlay.sessionLayersPlacement=5;
        XrGraphicsBindingOpenGLXlibKHR binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR};binding.next=&overlay;binding.xDisplay=graphics.display;binding.visualid=graphics.visual;binding.glxFBConfig=graphics.config;binding.glxDrawable=graphics.drawable;binding.glxContext=graphics.context;
        XrSessionCreateInfo create{XR_TYPE_SESSION_CREATE_INFO};create.next=&binding;create.systemId=system;XR_DO(xrCreateSession(instance,&create,&session));
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.poseInReferenceSpace.orientation.w=1;space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;XR_DO(xrCreateReferenceSpace(session,&space,&local));space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;XR_DO(xrCreateReferenceSpace(session,&space,&view));
        uint32_t n=0;XR_DO(xrEnumerateSwapchainFormats(session,0,&n,nullptr));std::vector<int64_t> formats(n);XR_DO(xrEnumerateSwapchainFormats(session,n,&n,formats.data()));
        if(std::find(formats.begin(),formats.end(),GL_SRGB8_ALPHA8)!=formats.end())format=GL_SRGB8_ALPHA8;else if(std::find(formats.begin(),formats.end(),GL_RGBA8)==formats.end())throw std::runtime_error("Runtime has no supported RGBA overlay format");
        XrActionSetCreateInfo action_set{XR_TYPE_ACTION_SET_CREATE_INFO};std::strcpy(action_set.actionSetName,"workspace");std::strcpy(action_set.localizedActionSetName,"Workspace");action_set.priority=100;XR_DO(xrCreateActionSet(instance,&action_set,&actions));
        paths={path("/user/hand/left"),path("/user/hand/right")};
        auto action=[this](const char* name,XrActionType type,XrAction& value){XrActionCreateInfo create{XR_TYPE_ACTION_CREATE_INFO};std::strcpy(create.actionName,name);std::strcpy(create.localizedActionName,name);create.actionType=type;create.countSubactionPaths=2;create.subactionPaths=paths.data();XR_DO(xrCreateAction(actions,&create,&value));};
        action("aim",XR_ACTION_TYPE_POSE_INPUT,aim);action("click",XR_ACTION_TYPE_FLOAT_INPUT,click);action("grab",XR_ACTION_TYPE_FLOAT_INPUT,grab);action("scroll",XR_ACTION_TYPE_FLOAT_INPUT,scroll);action("toggle",XR_ACTION_TYPE_BOOLEAN_INPUT,toggle);
        for(const char* profile:{"/interaction_profiles/oculus/touch_controller","/interaction_profiles/valve/index_controller","/interaction_profiles/microsoft/motion_controller","/interaction_profiles/khr/simple_controller"}){
            std::vector<XrActionSuggestedBinding> bindings;
            bool simple=std::string(profile).find("simple_controller")!=std::string::npos;
            for(const char* hand:{"left","right"}){
                auto prefix=std::string("/user/hand/")+hand;
                if(std::string(hand)=="left")bindings.push_back({toggle,path((prefix+(simple?"/input/menu/click":std::string(profile).find("oculus")!=std::string::npos?"/input/menu/click":"/input/thumbstick/click")).c_str())});
                bindings.push_back({aim,path((prefix+"/input/aim/pose").c_str())});
                bindings.push_back({click,path((prefix+(simple?"/input/select/click":"/input/trigger/value")).c_str())});
                if(!simple){bindings.push_back({grab,path((prefix+"/input/squeeze/value").c_str())});bindings.push_back({scroll,path((prefix+"/input/thumbstick/y").c_str())});}
            }
            XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};suggestion.interactionProfile=path(profile);suggestion.countSuggestedBindings=bindings.size();suggestion.suggestedBindings=bindings.data();
            auto result=xrSuggestInteractionProfileBindings(instance,&suggestion);if(XR_FAILED(result))g_warning("Overlay controller profile %s unavailable: %d",profile,result);
        }
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&actions;XR_DO(xrAttachSessionActionSets(session,&attach));
        for(unsigned i=0;i<2;++i){XrActionSpaceCreateInfo info{XR_TYPE_ACTION_SPACE_CREATE_INFO};info.action=aim;info.subactionPath=paths[i];info.poseInActionSpace.orientation.w=1;XR_DO(xrCreateActionSpace(session,&info,&hand_spaces[i]));}
        g_message("PCVR_OVERLAY backend=OpenXR_EXTX_overlay single_pcvr_stream=true");
    }
    void destroy(){
        if(session){for(auto chain:chains)xrDestroySwapchain(chain);for(auto space:hand_spaces)if(space)xrDestroySpace(space);if(view)xrDestroySpace(view);if(local)xrDestroySpace(local);xrDestroySession(session);session=XR_NULL_HANDLE;}
        if(actions)xrDestroyActionSet(actions);
        if(instance)xrDestroyInstance(instance);
        instance=XR_NULL_HANDLE;
    }
    ~OpenXr(){destroy();}
    void swapchain(Panel& p){
        if(p.swapchain && sizes.at(p.swapchain)==std::pair{p.width,p.height})return;
        if(p.swapchain){xrDestroySwapchain(p.swapchain);sizes.erase(p.swapchain);std::erase(chains,p.swapchain);p.swapchain=XR_NULL_HANDLE;}
        XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;create.format=format;create.sampleCount=1;create.width=p.width;create.height=p.height;create.faceCount=1;create.arraySize=1;create.mipCount=1;
        XR_DO(xrCreateSwapchain(session,&create,&p.swapchain));chains.push_back(p.swapchain);sizes.emplace(p.swapchain,std::pair{p.width,p.height});
        uint32_t count=0;XR_DO(xrEnumerateSwapchainImages(p.swapchain,0,&count,nullptr));p.images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});XR_DO(xrEnumerateSwapchainImages(p.swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(p.images.data())));
    }
    void run(Workspace& workspace){
        while(running){
            XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
            while(xrPollEvent(instance,&event)==XR_SUCCESS){
                if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){auto state=reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;
                    if(state==XR_SESSION_STATE_READY){XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;XR_DO(xrBeginSession(session,&begin));begun=true;}
                    if(state==XR_SESSION_STATE_STOPPING){workspace.release();if(begun)XR_DO(xrEndSession(session));begun=false;}
                    if(state==XR_SESSION_STATE_EXITING||state==XR_SESSION_STATE_LOSS_PENDING)running=0;
                }else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)running=0;
                event={XR_TYPE_EVENT_DATA_BUFFER};
            }
            if(!running)break;
            if(!begun){while(g_main_context_iteration(nullptr,false)){}g_usleep(20000);continue;}
            XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState frame{XR_TYPE_FRAME_STATE};XR_DO(xrWaitFrame(session,&wait,&frame));XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};XR_DO(xrBeginFrame(session,&begin));
            workspace.poll();
            XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};XR_DO(xrLocateSpace(view,local,frame.predictedDisplayTime,&head));
            XrActiveActionSet active{actions,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;auto sync_result=xrSyncActions(session,&sync);if(XR_FAILED(sync_result))xr_check(sync_result,"xrSyncActions");
            XrActionStateGetInfo toggle_get{XR_TYPE_ACTION_STATE_GET_INFO};toggle_get.action=toggle;toggle_get.subactionPath=paths[0];XrActionStateBoolean toggle_state{XR_TYPE_ACTION_STATE_BOOLEAN};XR_DO(xrGetActionStateBoolean(session,&toggle_get,&toggle_state));
            workspace.visibility_button(toggle_state.isActive&&toggle_state.currentState);
            std::array<Controller,2> controllers{};
            for(unsigned i=0;i<2;++i){
                XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};XR_DO(xrLocateSpace(hand_spaces[i],local,frame.predictedDisplayTime,&location));
                auto& c=controllers[i];c.valid=(location.locationFlags&(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))==(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);c.aim=from_xr(location.pose);
                auto value=[&](XrAction action){XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.action=action;get.subactionPath=paths[i];XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};XR_DO(xrGetActionStateFloat(session,&get,&state));return state.isActive?state.currentState:0.f;};
                c.click=value(click)>.65f;c.grab=value(grab)>.65f;c.scroll=value(scroll);
            }
            if((head.locationFlags&(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))==(XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))workspace.controls(controllers,from_xr(head.pose));else workspace.release();
            std::vector<XrCompositionLayerQuad> quads;quads.reserve(workspace.panels.size());
            if(frame.shouldRender&&workspace.visibility.visible)for(auto& panel:workspace.panels){auto& p=*panel;if(!p.capture||!p.width)continue;swapchain(p);uint32_t index=0;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};XR_DO(xrAcquireSwapchainImage(p.swapchain,&acquire,&index));
                XrSwapchainImageWaitInfo image_wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};image_wait.timeout=XR_INFINITE_DURATION;XR_DO(xrWaitSwapchainImage(p.swapchain,&image_wait));workspace.draw(p,p.images.at(index).image);XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};XR_DO(xrReleaseSwapchainImage(p.swapchain,&release));
                XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};quad.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;quad.space=local;quad.eyeVisibility=XR_EYE_VISIBILITY_BOTH;quad.subImage.swapchain=p.swapchain;quad.subImage.imageRect.extent={int32_t(p.width),int32_t(p.height)};quad.pose=to_xr(p.pose);quad.size={p.metres,p.physical_height()};quads.push_back(quad);
            }
            std::vector<const XrCompositionLayerBaseHeader*> layers;for(auto& quad:quads)layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quad));
            XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frame.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;end.layerCount=layers.size();end.layers=layers.data();XR_DO(xrEndFrame(session,&end));
        }
        workspace.release();if(begun)xrRequestExitSession(session);
    }
};
