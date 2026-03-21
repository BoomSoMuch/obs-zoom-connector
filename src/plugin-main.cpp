#include <obs-module.h>
#include <util/platform.h>
#include <string>
#include <windows.h>

// 1. Master Dictionaries
#include "zoom_sdk_def.h"
#include "zoom_sdk_raw_data_def.h"

// 2. Core Engine
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"

// 3. Meeting Components
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"
#include "meeting_service_components/meeting_ui_ctrl_interface.h"

// 4. Raw Data
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"

static obs_source_t* g_participantSource = nullptr;
static uint64_t g_next_timestamp = 0;

// --- VIDEO RENDERER ---
class ZoomVideoCatcher : public ZOOM_SDK_NAMESPACE::IZoomSDKRendererDelegate {
public:
    virtual void onRawDataFrameReceived(YUVRawDataI420* data) override {
        if (!g_participantSource) return;
        
        struct obs_source_frame obs_frame = {};
        obs_frame.format = VIDEO_FORMAT_I420; 
        obs_frame.width = data->GetStreamWidth();
        obs_frame.height = data->GetStreamHeight();
        obs_frame.data[0] = (uint8_t*)data->GetYBuffer();
        obs_frame.data[1] = (uint8_t*)data->GetUBuffer();
        obs_frame.data[2] = (uint8_t*)data->GetVBuffer();
        obs_frame.linesize[0] = obs_frame.width;
        obs_frame.linesize[1] = obs_frame.width / 2;
        obs_frame.linesize[2] = obs_frame.width / 2;
        
        obs_frame.timestamp = os_gettime_ns();
        obs_source_output_video(g_participantSource, &obs_frame);
    }
    virtual void onRawDataStatusChanged(RawDataStatus status) override {}
    virtual void onRendererBeDestroyed() override {}
};

static ZoomVideoCatcher g_videoCatcher;
static ZOOM_SDK_NAMESPACE::IZoomSDKRenderer* g_videoRenderer = nullptr;

// --- RECORDING CONTROLLER ---
class ZoomRecordingListener : public ZOOM_SDK_NAMESPACE::IMeetingRecordingCtrlEvent {
public:
    virtual void onRecordPrivilegeChanged(bool bCanRec) override {
        if (bCanRec) {
            blog(LOG_INFO, "[Zoom to OBS] Permission detected. Starting Pipeline...");
            ZOOM_SDK_NAMESPACE::IMeetingService* ms = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&ms);
            if (ms && ms->GetMeetingRecordingController()) {
                ms->GetMeetingRecordingController()->StartRawRecording();
                
                ZOOM_SDK_NAMESPACE::createRenderer(&g_videoRenderer, &g_videoCatcher);
                if (g_videoRenderer) {
                    g_videoRenderer->setRawDataResolution(ZOOM_SDK_NAMESPACE::ZoomSDKResolution_720P);
                    auto part_ctrl = ms->GetMeetingParticipantsController();
                    if (part_ctrl && part_ctrl->GetParticipantsList()) {
                        unsigned int first_user = part_ctrl->GetParticipantsList()->GetItem(0);
                        g_videoRenderer->subscribe(first_user, ZOOM_SDK_NAMESPACE::RAW_DATA_TYPE_VIDEO);
                    }
                }
            }
        }
    }
    virtual void onRecordingStatus(ZOOM_SDK_NAMESPACE::RecordingStatus s) override {}
    virtual void onCloudRecordingStatus(ZOOM_SDK_NAMESPACE::RecordingStatus s) override {}
    virtual void onLocalRecordingPrivilegeRequestStatus(ZOOM_SDK_NAMESPACE::RequestLocalRecordingStatus s) override {}
    virtual void onRequestCloudRecordingResponse(ZOOM_SDK_NAMESPACE::RequestStartCloudRecordingStatus s) override {}
    virtual void onLocalRecordingPrivilegeRequested(ZOOM_SDK_NAMESPACE::IRequestLocalRecordingPrivilegeHandler* h) override {}
    virtual void onStartCloudRecordingRequested(ZOOM_SDK_NAMESPACE::IRequestStartCloudRecordingHandler* h) override {}
    virtual void onCloudRecordingStorageFull(time_t g) override {}
    virtual void onEnableAndStartSmartRecordingRequested(ZOOM_SDK_NAMESPACE::IRequestEnableAndStartSmartRecordingHandler* h) override {}
    virtual void onSmartRecordingEnableActionCallback(ZOOM_SDK_NAMESPACE::ISmartRecordingEnableActionHandler* h) override {}
};
static ZoomRecordingListener g_recordingListener;

// --- MEETING SERVICE ---
class ZoomMeetingListener : public ZOOM_SDK_NAMESPACE::IMeetingServiceEvent {
public:
    virtual void onMeetingStatusChanged(ZOOM_SDK_NAMESPACE::MeetingStatus status, int iResult = 0) override {
        if (status == ZOOM_SDK_NAMESPACE::MEETING_STATUS_INMEETING) {
            blog(LOG_INFO, "[Zoom to OBS] Successfully in meeting.");
            ZOOM_SDK_NAMESPACE::IMeetingService* ms = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&ms);
            if (ms) {
                auto rec_ctrl = ms->GetMeetingRecordingController();
                if (rec_ctrl) {
                    rec_ctrl->SetEvent(&g_recordingListener);
                    // Force the check—since you logged in manually, this should return true immediately
                    g_recordingListener.onRecordPrivilegeChanged(true);
                }
            }
        }
    }
    virtual void onMeetingStatisticsWarningNotification(ZOOM_SDK_NAMESPACE::StatisticsWarningType t) override {}
    virtual void onMeetingParameterNotification(const ZOOM_SDK_NAMESPACE::MeetingParameter* p) override {}
    virtual void onSuspendParticipantsActivities() override {}
    virtual void onAICompanionActiveChangeNotice(bool b) override {}
    virtual void onMeetingTopicChanged(const zchar_t* s) override {}
    virtual void onMeetingFullToWatchLiveStream(const zchar_t* s) override {}
    virtual void onUserNetworkStatusChanged(ZOOM_SDK_NAMESPACE::MeetingComponentType t, ZOOM_SDK_NAMESPACE::ConnectionQuality l, unsigned int u, bool up) override {}
};
static ZoomMeetingListener g_meetingListener;

// --- AUTH SERVICE ---
class ZoomAuthListener : public ZOOM_SDK_NAMESPACE::IAuthServiceEvent {
public:
    virtual void onAuthenticationReturn(ZOOM_SDK_NAMESPACE::AuthResult ret) override {
        if (ret == ZOOM_SDK_NAMESPACE::AUTHRET_SUCCESS) {
            blog(LOG_INFO, "[Zoom to OBS] SDK Auth OK. Popping Login Window...");
            
            ZOOM_SDK_NAMESPACE::IMeetingService* ms = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&ms);
            if (ms) {
                ms->SetEvent(&g_meetingListener);
                // THIS TRIGGERS THE MANUAL LOGIN UI
                ms->GetUIController()->ShowChatSignWND(); 
            }
        }
    }
    virtual void onLoginReturnWithReason(ZOOM_SDK_NAMESPACE::LOGINSTATUS ret, ZOOM_SDK_NAMESPACE::IAccountInfo* p, ZOOM_SDK_NAMESPACE::LoginFailReason r) override {
        if (ret == ZOOM_SDK_NAMESPACE::LOGIN_SUCCESS) {
            blog(LOG_INFO, "[Zoom to OBS] Manual Login Success! You can now join your meeting.");
        }
    }
    virtual void onLogout() override {}
    virtual void onZoomIdentityExpired() override {}
    virtual void onZoomAuthIdentityExpired() override {}
};
static ZoomAuthListener g_authListener;

// --- OBS REGISTRATION ---
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    struct obs_source_info info = {};
    info.id = "zoom_participant_source";
    info.type = OBS_SOURCE_TYPE_INPUT;
    info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    info.get_name = [](void*) { return "Zoom Participant"; };
    info.create = [](obs_data_t*, obs_source_t* s) { g_participantSource = s; return (void*)1; };
    info.destroy = [](void*) { g_participantSource = nullptr; };
    obs_register_source(&info);

    ZOOM_SDK_NAMESPACE::InitParam ip;
    ip.strWebDomain = L"https://zoom.us";
    if (ZOOM_SDK_NAMESPACE::InitSDK(ip) == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
        ZOOM_SDK_NAMESPACE::IAuthService* auth = nullptr;
        ZOOM_SDK_NAMESPACE::CreateAuthService(&auth);
        if (auth) {
            auth->SetEvent(&g_authListener);
            ZOOM_SDK_NAMESPACE::AuthContext ctx;
            ctx.jwt_token = L"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJhcHBLZXkiOiJZNzNqelFSbVF4aWhoNFo3MnFSMnRnIiwiaWF0IjoxNzc0MDUwMDAwLCJleHAiOjE3NzY2NDIwMDAsInRva2VuRXhwIjoxNzc2NjQyMDAwLCJyb2xlIjoxLCJ1c2VyRW1haWwiOiJEYXZpZEBMZXRzRG9WaWRlby5jb20ifQ.1ldmzxzK-gdzWJkxr7KkkwnYq8qEnbMGVTJFihAhuEA"; // Keep using your JWT for SDK Auth
            auth->SDKAuth(ctx);
        }
    }
    return true;
}

void obs_module_unload(void) {}
