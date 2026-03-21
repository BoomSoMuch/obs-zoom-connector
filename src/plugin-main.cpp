// 3. Meeting Components (AUDIO MUST GO BEFORE PARTICIPANTS!)
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_recording_interface.h" // <-- ADDED FOR RECORDING PERMISSIONS
#include "meeting_service_components/meeting_recording_interface.h"

// 4. Raw Data LAST
#include "rawdata/rawdata_renderer_interface.h"
@@ -55,7 +55,7 @@ static ZoomVideoCatcher g_videoCatcher;

// Create a global pointer to hold the pipeline open
static ZOOM_SDK_NAMESPACE::IZoomSDKRenderer* g_videoRenderer = nullptr;
// ---------------------------------


// --- 4. THE RECORDING WALKIE-TALKIE ---
class ZoomRecordingListener : public ZOOM_SDK_NAMESPACE::IMeetingRecordingCtrlEvent {
@@ -127,10 +127,9 @@ class ZoomRecordingListener : public ZOOM_SDK_NAMESPACE::IMeetingRecordingCtrlEv

// Create the global instance
static ZoomRecordingListener g_recordingListener;
// ----------------------------------------------


// --- 1. THE ZOOM MEETING WALKIE-TALKIE (Must go first!) ---
// --- 1. THE ZOOM MEETING WALKIE-TALKIE ---
class ZoomMeetingListener : public ZOOM_SDK_NAMESPACE::IMeetingServiceEvent {
public:
virtual void onMeetingStatusChanged(ZOOM_SDK_NAMESPACE::MeetingStatus status, int iResult = 0) override {
@@ -170,7 +169,7 @@ class ZoomMeetingListener : public ZOOM_SDK_NAMESPACE::IMeetingServiceEvent {

// Create the global instance BEFORE the Auth Listener tries to use it
static ZoomMeetingListener g_meetingListener;
// ----------------------------------------------------------


// --- 2. THE ZOOM WALKIE-TALKIE (AUTH LISTENER) ---
class ZoomAuthListener : public ZOOM_SDK_NAMESPACE::IAuthServiceEvent {
@@ -224,7 +223,7 @@ class ZoomAuthListener : public ZOOM_SDK_NAMESPACE::IAuthServiceEvent {

// Create one global instance of our listener
static ZoomAuthListener g_authListener;
// ----------------------------------------------


// ----------------------------------------------------------------------------
// THE INDEPENDENT ZOOM SOURCE CLASS
@@ -316,3 +315,95 @@ class ZoomSource {
}

~ZoomSource() {
        webSocket.stop();
        if (parser) av_parser_close(parser);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
    }
};

// ----------------------------------------------------------------------------
// OBS C-API BRIDGES
// ----------------------------------------------------------------------------
void* zg_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Gallery"); }
void* zp_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Participant"); }
void* zs_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Screenshare"); }

void z_destroy(void* data) { 
    delete static_cast<ZoomSource*>(data); 
}

// ----------------------------------------------------------------------------
// PLUGIN REGISTRATION
// ----------------------------------------------------------------------------
struct obs_source_info zoom_participant_info = {};
struct obs_source_info zoom_screenshare_info = {};
struct obs_source_info zoom_gallery_info = {};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    zoom_participant_info.get_name = [](void*) { return "Zoom Participant"; };
    zoom_participant_info.create = zp_create;
    zoom_participant_info.destroy = z_destroy;

    zoom_screenshare_info.id = "zoom_screenshare_source";
    zoom_screenshare_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_screenshare_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    zoom_screenshare_info.get_name = [](void*) { return "Zoom Screenshare"; };
    zoom_screenshare_info.create = zs_create;
    zoom_screenshare_info.destroy = z_destroy;

    zoom_gallery_info.id = "zoom_gallery_source";
    zoom_gallery_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_gallery_info.output_flags = OBS_SOURCE_ASYNC_VIDEO; 
    zoom_gallery_info.get_name = [](void*) { return "Zoom Gallery"; };
    zoom_gallery_info.create = zg_create;
    zoom_gallery_info.destroy = z_destroy;

    obs_register_source(&zoom_participant_info);
    obs_register_source(&zoom_screenshare_info);
    obs_register_source(&zoom_gallery_info);

// --- WAKE UP THE ZOOM ENGINE ---
    ZOOM_SDK_NAMESPACE::InitParam initParam;
    initParam.strWebDomain = L"https://zoom.us";
    
    ZOOM_SDK_NAMESPACE::SDKError err = ZOOM_SDK_NAMESPACE::InitSDK(initParam);
    
   if (err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
        blog(LOG_INFO, "[Zoom to OBS] SUCCESS: Zoom Meeting SDK Initialized!");
        
        // --- AUTHENTICATE THE ENGINE ---
        ZOOM_SDK_NAMESPACE::IAuthService* auth_service = nullptr;
        err = ZOOM_SDK_NAMESPACE::CreateAuthService(&auth_service);
        
        if (err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS && auth_service) {
            
            auth_service->SetEvent(&g_authListener);

            ZOOM_SDK_NAMESPACE::AuthContext authContext;
            
            // PASTE YOUR JWT TOKEN HERE:
            authContext.jwt_token = L"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJhcHBLZXkiOiJzWUlqWGpqelFkMmJOQ2dYMXZKWU1BIiwiaWF0IjoxNzEwODYwMDAwLCJleHAiOjE3MTE0NjQ4MDAsInRva2VuRXhwIjoxNzExNDY0ODAwfQ.PwVVrG2xkj2_LtWhFoeqF3EV7tEy9ERWMzQwmDKKVWE"; 
            
            err = auth_service->SDKAuth(authContext);
            if (err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
                blog(LOG_INFO, "[Zoom to OBS] SUCCESS: Auth request sent to Zoom!");
            } else {
                blog(LOG_ERROR, "[Zoom to OBS] ERROR: Auth request failed. Error: %d", err);
            }
        }
    } else {
        blog(LOG_ERROR, "[Zoom to OBS] ERROR: Zoom Meeting SDK failed to initialize. Error Code: %d", err);
    }
    
    return true;
}

void obs_module_unload(void) {}
