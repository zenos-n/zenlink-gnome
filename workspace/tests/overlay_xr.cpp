// SPDX-License-Identifier: MIT
// Run the real OpenXR backend against a disposable API/GL fixture, never a runtime.
#include "../host/overlay_gl.hpp"
#define XR_USE_PLATFORM_XLIB
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "../host/overlay_math.hpp"
#include "../host/overlay_ui.hpp"
#include <glib.h>
#include <array>
#include <map>
#include <memory>
#include <cstring>
#include <cassert>
#include <iostream>
using namespace workspace::overlay;
static bool running=true,ready=false,exiting=false,advertise=true;
static int frames=0,created=0,destroyed=0,instances=0;
static GLuint image=0;static uint32_t sw=0,sh=0;
static void xr_check(XrResult r,const char* name){if(XR_FAILED(r))throw std::runtime_error(name);}
#define XR_DO(expression) xr_check((expression),#expression)
struct Panel {XrSwapchain swapchain=XR_NULL_HANDLE;std::vector<XrSwapchainImageOpenGLKHR> images;unsigned width=64,height=64;bool capture=true;Pose pose{{},{0,0,-2}};float metres=1.2;float physical_height(){return metres*height/width;}};
struct Controller {Pose aim;bool valid=false,click=false,grab=false;float scroll=0;};
struct Workspace {
    Graphics& gl;OverlayVisibility visibility;std::vector<std::unique_ptr<Panel>> panels;
    explicit Workspace(Graphics& value):gl(value){panels.push_back(std::make_unique<Panel>());}
    void visibility_button(bool pressed){visibility.update(pressed);}
    void release(){}void controls(const std::array<Controller,2>&,Pose){}
    void poll(){if(frames==1){panels[0]->width=80;panels[0]->height=48;}auto& p=*panels[0];gl.upload(p.width,p.height,std::vector<uint8_t>(p.width*p.height*3,128));}
    void draw(Panel& p,GLuint texture){gl.draw(texture,p.width,p.height,15);}
};
static Pose from_xr(XrPosef p){return{{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w},{p.position.x,p.position.y,p.position.z}};}
static XrPosef to_xr(Pose p){return{{p.rotation.x,p.rotation.y,p.rotation.z,p.rotation.w},{p.position.x,p.position.y,p.position.z}};}
#include "../host/overlay_openxr.hpp"
extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(const char*,uint32_t capacity,uint32_t* count,XrExtensionProperties* out){*count=advertise?2:1;if(capacity){std::strcpy(out[0].extensionName,XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);if(advertise)std::strcpy(out[1].extensionName,XR_EXTX_OVERLAY_EXTENSION_NAME);}return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo* info,XrInstance* out){assert(info->enabledExtensionCount==2);++instances;*out=reinterpret_cast<XrInstance>(1);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(XrInstance){--instances;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(XrInstance,const XrSystemGetInfo*,XrSystemId* out){*out=1;return XR_SUCCESS;}
static XrResult requirements(XrInstance,XrSystemId,XrGraphicsRequirementsOpenGLKHR* out){out->minApiVersionSupported=XR_MAKE_VERSION(3,3,0);out->maxApiVersionSupported=XR_MAKE_VERSION(5,0,0);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance,const char* name,PFN_xrVoidFunction* fn){assert(std::string(name)=="xrGetOpenGLGraphicsRequirementsKHR");*fn=reinterpret_cast<PFN_xrVoidFunction>(requirements);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSession(XrInstance,const XrSessionCreateInfo* info,XrSession* out){auto* gl=static_cast<const XrGraphicsBindingOpenGLXlibKHR*>(info->next);assert(gl&&gl->type==XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR&&gl->glxContext);auto* overlay=static_cast<const XrSessionCreateInfoOverlayEXTX*>(gl->next);assert(overlay&&overlay->type==XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX&&overlay->sessionLayersPlacement==5);*out=reinterpret_cast<XrSession>(1);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(XrSession){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession,const XrReferenceSpaceCreateInfo*,XrSpace* out){*out=reinterpret_cast<XrSpace>(1);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainFormats(XrSession,uint32_t capacity,uint32_t* count,int64_t* formats){*count=1;if(capacity)formats[0]=GL_SRGB8_ALPHA8;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(XrInstance,const char*,XrPath* out){static XrPath next=0;*out=++next;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(XrInstance,const XrActionSetCreateInfo*,XrActionSet* out){*out=reinterpret_cast<XrActionSet>(1);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(XrActionSet){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(XrActionSet,const XrActionCreateInfo*,XrAction* out){*out=reinterpret_cast<XrAction>(1);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(XrInstance,const XrInteractionProfileSuggestedBinding*){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(XrSession,const XrSessionActionSetsAttachInfo*){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(XrSession,const XrActionSpaceCreateInfo*,XrSpace* out){*out=reinterpret_cast<XrSpace>(1);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(XrInstance,XrEventDataBuffer* out){if(!ready||frames>=3&&!exiting){auto* event=reinterpret_cast<XrEventDataSessionStateChanged*>(out);event->type=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;event->state=ready?XR_SESSION_STATE_EXITING:XR_SESSION_STATE_READY;if(ready)exiting=true;ready=true;return XR_SUCCESS;}return XR_EVENT_UNAVAILABLE;}
XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(XrSession,const XrSessionBeginInfo*){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession,const XrFrameWaitInfo*,XrFrameState* state){state->shouldRender=true;state->predictedDisplayTime=frames+1;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession,const XrFrameBeginInfo*){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(XrSpace,XrSpace,XrTime,XrSpaceLocation* location){location->locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;location->pose.orientation.w=1;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(XrSession,const XrActionsSyncInfo*){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(XrSession,const XrActionStateGetInfo*,XrActionStateFloat* state){state->isActive=false;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(XrSession,const XrActionStateGetInfo*,XrActionStateBoolean* state){state->isActive=false;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out){assert(info->format==GL_SRGB8_ALPHA8);sw=info->width;sh=info->height;glGenTextures(1,&image);glBindTexture(GL_TEXTURE_2D,image);glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,sw,sh,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);*out=reinterpret_cast<XrSwapchain>(uintptr_t(++created));return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain){++destroyed;glDeleteTextures(1,&image);return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out){*count=1;if(capacity)reinterpret_cast<XrSwapchainImageOpenGLKHR*>(out)->image=image;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* index){*index=0;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain,const XrSwapchainImageWaitInfo*){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain,const XrSwapchainImageReleaseInfo*){return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession,const XrFrameEndInfo* info){assert(info->layerCount==1);auto* quad=reinterpret_cast<const XrCompositionLayerQuad*>(info->layers[0]);assert(quad->type==XR_TYPE_COMPOSITION_LAYER_QUAD&&quad->subImage.imageRect.extent.width==int(sw)&&quad->subImage.imageRect.extent.height==int(sh));assert(quad->layerFlags&XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);++frames;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL xrRequestExitSession(XrSession){return XR_SUCCESS;}
}
int main(){Graphics gl;{OpenXr overlay(gl);Workspace workspace(gl);overlay.run(workspace);}assert(frames==3&&created==2&&destroyed==2&&instances==0);advertise=false;bool rejected=false;try{OpenXr unavailable(gl);}catch(const std::exception&){rejected=true;}assert(rejected&&instances==0);assert(glGetError()==GL_NO_ERROR);std::cout<<"OVERLAY_XR_PASS required extension overlay session quad composition resize cleanup unsupported rejection\n";}
