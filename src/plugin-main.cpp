#include <obs-module.h>
#include <util/platform.h>
#include <string>
#include <windows.h>

// 1. Master Dictionaries FIRST
#include "zoom_sdk_def.h"
#include "zoom_sdk_raw_data_def.h"

// 2. Core Engine NEXT
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"

// 3. Meeting Components
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"

// 4. Raw Data LAST
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"

// --- THE OBS VISUAL TARGET ---
static obs_source_t* g_participantSource = nullptr;

// --- 3. THE ZOOM VIDEO CATCHER ---
class ZoomVideoCatcher : public ZOOM_SDK_NAMESPACE::IZoomSDKRendererDelegate {
public:
    virtual void onRawDataFrameReceived(YUVRawDataI420* data) override {
        if (!g_participantSource) return;

        unsigned int width = data->GetStreamWidth();
        unsigned int height = data->GetStreamHeight();
        
        static int frameCount = 0;
        if (frameCount % 300 == 0) { 
            blog(LOG_INFO, "[ISO for OBS] CATCHER ALERT: Painting Video Frame! Resolution: %dx%d", width, height);
        }
        frameCount++;

        struct obs_source_frame obs_frame = {};
        obs_frame.format = VIDEO_FORMAT_I420; 
        obs_frame.width = width;
        obs_frame.height = height;
        
        obs_frame.data[0] = (uint8_t*)data->GetYBuffer();
        obs_frame.data[1] = (uint8_t*)data->GetUBuffer();
        obs_frame.data[2] = (uint8_t*)data->GetVBuffer();
        
        obs_frame.linesize[0] = width;
        obs_frame.linesize[1] = width / 2;
        obs_frame.linesize[2] = width / 2;
        
        video_format_get_parameters(VIDEO_CS_DEFAULT, VIDEO_RANGE_PARTIAL, 
                                    obs_frame.color_matrix, 
                                    obs_frame.color_range_min, 
                                    obs_frame.color_range_max);
        
        obs_frame.timestamp = data->GetTimeStamp() * 1000000ULL;
        
        obs_source_output_video(g_participantSource, &obs_frame);
    }

    virtual void onRawDataStatusChanged(RawDataStatus status) override {}
    virtual void onRendererBeDestroyed() override {}
};

static ZoomVideoCatcher g_videoCatcher;
static ZOOM_SDK_NAMESPACE::IZoomSDKRenderer* g_videoRenderer = nullptr;


// --- 4. THE RECORDING WALKIE-TALKIE ---
class ZoomRecordingListener : public ZOOM_SDK_NAMESPACE::IMeetingRecordingCtrlEvent {
public:
    virtual void onRecordPrivilegeChanged(bool bCanRec) override {
        if (!bCanRec) return;
        
        blog(LOG_INFO, "[ISO for OBS] Host granted recording permission!");
        
        ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
        ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
        if (!meeting_service) return;

        ZOOM_SDK_NAMESPACE::IMeetingRecordingController* rec_ctrl = meeting_service->GetMeetingRecordingController();
        if (!rec_ctrl) return;

        ZOOM_SDK_NAMESPACE::SDKError rec_err = rec_ctrl->StartRawRecording();
        if (rec_err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
            blog(LOG_INFO, "[ISO for OBS] Raw Recording started. Creating video pipeline...");
            
            ZOOM_SDK_NAMESPACE::SDKError err = ZOOM_SDK_NAMESPACE::createRenderer(&g_videoRenderer, &g_videoCatcher);
            
            if (err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS && g_videoRenderer) {
                g_videoRenderer->setRawDataResolution(ZOOM_SDK_NAMESPACE::ZoomSDKResolution_720P);
                
                ZOOM_SDK_NAMESPACE::IMeetingParticipantsController* part_ctrl = meeting_service->GetMeetingParticipantsController();
                if (part_ctrl) {
                    ZOOM_SDK_NAMESPACE::IList<unsigned int>* userList = part_ctrl->GetParticipantsList();
                    if (userList && userList->GetCount() > 0) {
                        unsigned int target_user_id = userList->GetItem(0);
                        err = g_videoRenderer->subscribe(target_user_id, ZOOM_SDK_NAMESPACE::RAW_DATA_TYPE_VIDEO);
                        blog(LOG_INFO, "[ISO for OBS] Video Catcher attached to User ID %u! Subscribe result: %d", target_user_id, err);
                    }
                }
            } else {
                blog(LOG_ERROR, "[ISO for OBS] ERROR: Failed to create renderer. Code: %d", err);
            }
        } else {
            blog(LOG_ERROR, "[ISO for OBS] ERROR: Failed to start raw recording. Code: %d", rec_err);
        }
    }

    virtual void onRecordingStatus(ZOOM_SDK_NAMESPACE::RecordingStatus status) override {}
    virtual void onCloudRecordingStatus(ZOOM_SDK_NAMESPACE::RecordingStatus status) override {}
    virtual void onLocalRecordingPrivilegeRequestStatus(ZOOM_SDK_NAMESPACE::RequestLocalRecordingStatus status) override {}
    virtual void onRequestCloudRecordingResponse(ZOOM_SDK_NAMESPACE::RequestStartCloudRecordingStatus status) override {}
    virtual void onLocalRecordingPrivilegeRequested(ZOOM_SDK_NAMESPACE::IRequestLocalRecordingPrivilegeHandler* handler) override {}
    virtual void onStartCloudRecordingRequested(ZOOM_SDK_NAMESPACE::IRequestStartCloudRecordingHandler* handler) override {}
    virtual void onCloudRecordingStorageFull(time_t gracePeriodDate) override {}
    virtual void onEnableAndStartSmartRecordingRequested(ZOOM_SDK_NAMESPACE::IRequestEnableAndStartSmartRecordingHandler* handler) override {}
    virtual void onSmartRecordingEnableActionCallback(ZOOM_SDK_NAMESPACE::ISmartRecordingEnableActionHandler* handler) override {}
#if defined(WIN32)
    virtual void onRecording2MP4Done(bool bsuccess, int iResult, const zchar_t* szPath) override {}
    virtual void onRecording2MP4Processing(int iPercentage) override {}
    virtual void onCustomizedLocalRecordingSourceNotification(ZOOM_SDK_NAMESPACE::ICustomizedLocalRecordingLayoutHelper* layout_helper) override {}
#endif
#if defined(__linux__)
    virtual void onTranscodingStatusChanged(ZOOM_SDK_NAMESPACE::TranscodingStatus status, const zchar_t* path) override {}
#endif
};
static ZoomRecordingListener g_recordingListener;


// --- 1. THE ZOOM MEETING WALKIE-TALKIE ---
class ZoomMeetingListener : public ZOOM_SDK_NAMESPACE::IMeetingServiceEvent {
public:
    virtual void onMeetingStatusChanged(ZOOM_SDK_NAMESPACE::MeetingStatus status, int iResult = 0) override {
        blog(LOG_INFO, "[ISO for OBS] MEETING STATUS CHANGED: Status Code %d (Result: %d)", status, iResult);
        
        if (status == ZOOM_SDK_NAMESPACE::MEETING_STATUS_INMEETING) {
            blog(LOG_INFO, "[ISO for OBS] SUCCESS! WE ARE OFFICIALLY IN THE MEETING!");
            
            ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
            if (meeting_service) {
                ZOOM_SDK_NAMESPACE::IMeetingRecordingController* rec_ctrl = meeting_service->GetMeetingRecordingController();
                if (rec_ctrl) {
                    rec_ctrl->SetEvent(&g_recordingListener);
                    blog(LOG_INFO, "[ISO for OBS] Waiting for Host to click 'Allow to Record Local Files'...");
                }
            }
        }
    }
    virtual void onMeetingStatisticsWarningNotification(ZOOM_SDK_NAMESPACE::StatisticsWarningType type) override {}
    virtual void onMeetingParameterNotification(const ZOOM_SDK_NAMESPACE::MeetingParameter* meeting_param) override {}
    virtual void onSuspendParticipantsActivities() override {}
    virtual void onAICompanionActiveChangeNotice(bool bActive) override {}
    virtual void onMeetingTopicChanged(const zchar_t* sTopic) override {}
    virtual void onMeetingFullToWatchLiveStream(const zchar_t* sLiveStreamUrl) override {}
    virtual void onUserNetworkStatusChanged(ZOOM_SDK_NAMESPACE::MeetingComponentType type, ZOOM_SDK_NAMESPACE::ConnectionQuality level, unsigned int userId, bool uplink) override {}
#if defined(WIN32)
    virtual void onAppSignalPanelUpdated(ZOOM_SDK_NAMESPACE::IMeetingAppSignalHandler* pHandler) override {}
#endif
};
static ZoomMeetingListener g_meetingListener;


// --- 2. THE ZOOM WALKIE-TALKIE (AUTH LISTENER) ---
class ZoomAuthListener : public ZOOM_SDK_NAMESPACE::IAuthServiceEvent {
public:
    virtual void onAuthenticationReturn(ZOOM_SDK_NAMESPACE::AuthResult ret) override {
        blog(LOG_INFO, "[ISO for OBS] RADIO MESSAGE: Auth Return Status: %d", ret);
        
        if (ret == ZOOM_SDK_NAMESPACE::AUTHRET_SUCCESS) {
            blog(LOG_INFO, "[ISO for OBS] SUCCESS! Zoom Engine is fully logged in and ready!");
            
            ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
            
            if (meeting_service) {
                meeting_service->SetEvent(&g_meetingListener);
                
                ZOOM_SDK_NAMESPACE::JoinParam joinParam;
                joinParam.userType = ZOOM_SDK_NAMESPACE::SDK_UT_WITHOUT_LOGIN;
                
                ZOOM_SDK_NAMESPACE::JoinParam4WithoutLogin& param = joinParam.param.withoutloginuserJoin;
                param.meetingNumber = 7723013754ULL; // Your Meeting ID
                param.userName = L"OBS Camera Bot";
                param.psw = L""; 
                param.isAudioOff = true;
                param.isVideoOff = true;

                ZOOM_SDK_NAMESPACE::SDKError err = meeting_service->Join(joinParam);
                blog(LOG_INFO, "[ISO for OBS] Join command sent. Status: %d", err);
            }
        } else {
            blog(LOG_ERROR, "[ISO for OBS] ERROR: Zoom rejected our JWT. Error Code: %d", ret);
        }
    }
    virtual void onLoginReturnWithReason(ZOOM_SDK_NAMESPACE::LOGINSTATUS ret, ZOOM_SDK_NAMESPACE::IAccountInfo* pAccountInfo, ZOOM_SDK_NAMESPACE::LoginFailReason reason) override {}
    virtual void onLogout() override {}
    virtual void onZoomIdentityExpired() override {}
    virtual void onZoomAuthIdentityExpired() override {}
#if defined(WIN32)
    virtual void onNotificationServiceStatus(ZOOM_SDK_NAMESPACE::SDKNotificationServiceStatus status, ZOOM_SDK_NAMESPACE::SDKNotificationServiceError error) override {}
#endif
};
static ZoomAuthListener g_authListener;


// ----------------------------------------------------------------------------
// THE OBS SOURCE CLASS
// ----------------------------------------------------------------------------
class ZoomSource {
public:
    obs_source_t* source;
    std::string source_type;

    ZoomSource(obs_source_t* src, const std::string& source_type_name) {
        source = src;
        source_type = source_type_name;
        
        if (source_type == "Participant") {
            g_participantSource = source;
            blog(LOG_INFO, "[ISO for OBS] Zoom Participant added to Scene! Ready to paint.");
        }
    }

    ~ZoomSource() {
        if (source_type == "Participant") {
            g_participantSource = nullptr; 
        }
    }
};

// --- THE OBS UI (PROPERTIES WINDOW) ---
obs_properties_t* zp_get_properties(void *data) {
    obs_properties_t* props = obs_properties_create();
    
    obs_property_t* drop_down = obs_properties_add_list(props, 
                                                        "target_user",           
                                                        "Select Participant:",   
                                                        OBS_COMBO_TYPE_LIST, 
                                                        OBS_COMBO_FORMAT_INT);
    
    // Add some dummy names to prove the UI draws correctly
    obs_property_list_add_int(drop_down, "Auto-Select (First Person)", 0);
    obs_property_list_add_int(drop_down, "David (Dummy Host)", 123456);
    obs_property_list_add_int(drop_down, "Guest 1 (Dummy)", 987654);

    return props;
}

void zp_update(void *data, obs_data_t *settings) {
    int selected_id = (int)obs_data_get_int(settings, "target_user");
    blog(LOG_INFO, "[ISO for OBS] UI TRIGGERED! User selected ID: %d", selected_id);
}

// ----------------------------------------------------------------------------
// OBS C-API BRIDGES
// ----------------------------------------------------------------------------
void* zp_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Participant"); }
void* zs_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Screenshare"); }

void z_destroy(void* data) { delete static_cast<ZoomSource*>(data); }

// ----------------------------------------------------------------------------
// PLUGIN REGISTRATION
// ----------------------------------------------------------------------------
struct obs_source_info zoom_participant_info = {};
struct obs_source_info zoom_screenshare_info = {};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    zoom_participant_info.get_name = [](void*) { return "Zoom Participant"; };
    zoom_participant_info.create = zp_create;
    zoom_participant_info.destroy = z_destroy;
    zoom_participant_info.get_properties = zp_get_properties;
    zoom_participant_info.update = zp_update;

    zoom_screenshare_info.id = "zoom_screenshare_source";
    zoom_screenshare_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_screenshare_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    zoom_screenshare_info.get_name = [](void*) { return "Zoom Screenshare"; };
    zoom_screenshare_info.create = zs_create;
    zoom_screenshare_info.destroy = z_destroy;

    obs_register_source(&zoom_participant_info);
    obs_register_source(&zoom_screenshare_info);

// --- WAKE UP THE ZOOM ENGINE ---
    ZOOM_SDK_NAMESPACE::InitParam initParam = {}; // <-- ADDED = {} TO ZERO OUT MEMORY
    initParam.strWebDomain = L"https://zoom.us";
    
    ZOOM_SDK_NAMESPACE::SDKError err = ZOOM_SDK_NAMESPACE::InitSDK(initParam);
    blog(LOG_INFO, "[ISO for OBS] INIT SDK STATUS: %d", err);
    
   if (err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
        ZOOM_SDK_NAMESPACE::IAuthService* auth_service = nullptr;
        err = ZOOM_SDK_NAMESPACE::CreateAuthService(&auth_service);
        
        if (err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS && auth_service) {
            auth_service->SetEvent(&g_authListener);
            
            ZOOM_SDK_NAMESPACE::AuthContext authContext = {}; // <-- ADDED = {} TO ZERO OUT MEMORY
            
            // --- PASTE YOUR JWT HERE ---
            authContext.jwt_token = L"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJhcHBLZXkiOiJzWUlqWGpqelFkMmJOQ2dYMXZKWU1BIiwiaWF0IjoxNzEwODYwMDAwLCJleHAiOjE3MTE0NjQ4MDAsInRva2VuRXhwIjoxNzExNDY0ODAwfQ.PwVVrG2xkj2_LtWhFoeqF3EV7tEy9ERWMzQwmDKKVWE"; 
            
            ZOOM_SDK_NAMESPACE::SDKError auth_err = auth_service->SDKAuth(authContext);
            blog(LOG_INFO, "[ISO for OBS] SDK AUTH REQUEST SENT. STATUS: %d", auth_err);
        }
    }
    return true;
}

void obs_module_unload(void) {}


