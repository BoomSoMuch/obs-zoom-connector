#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <obs-module.h>
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"
#include "zoom_sdk_raw_data_def.h"
#include "rawdata/rawdata_video_helper_interface.h"

using namespace ZOOMSDK;

// --- 1. GLOBAL INSTANCES & DELEGATES ---
class ZoomAuthListener;
class ZoomMeetingListener;
class VideoRawDataHandler;

ZoomAuthListener* authListener = nullptr;
ZoomMeetingListener* meetingListener = nullptr;
VideoRawDataHandler* videoHandler = nullptr;
IAuthService* g_pAuthService = nullptr;
IMeetingService* g_pMeetingService = nullptr;

// Handler to receive video frames from Zoom and give them to OBS
class VideoRawDataHandler : public IZoomSDKVideoRawDataDelegate {
public:
    void onRawDataFrameReceived(YUVRawDataI420* data) override {
        // This is where the video "magic" happens.
        // When we get a frame from Zoom, we tell OBS to display it.
        obs_source_t* source = obs_get_source_by_name("Zoom Participant");
        if (source) {
            struct obs_source_frame obsFrame = {};
            obsFrame.data[0] = (uint8_t*)data->GetYBuffer();
            obsFrame.data[1] = (uint8_t*)data->GetUBuffer();
            obsFrame.data[2] = (uint8_t*)data->GetVBuffer();
            obsFrame.linesize[0] = data->GetStreamWidth();
            obsFrame.linesize[1] = data->GetStreamWidth() / 2;
            obsFrame.linesize[2] = data->GetStreamWidth() / 2;
            obsFrame.width = data->GetStreamWidth();
            obsFrame.height = data->GetStreamHeight();
            obsFrame.format = VIDEO_FORMAT_I420;

            obs_source_output_video(source, &obsFrame);
            obs_source_release(source);
        }
    }
    void onRawDataStatusChanged(RawDataStatus status) override {}
};

// --- 2. JOIN MEETING LOGIC ---
void JoinMeeting() {
    if (!g_pMeetingService) return;

    JoinParam joinParam;
    joinParam.userType = SDK_UT_WITHOUT_LOGIN;
    joinParam.param.withoutloginuserJoin.meetingNumber = 7723013754;
    joinParam.param.withoutloginuserJoin.userName = L"OBS Connector";
    joinParam.param.withoutloginuserJoin.psw = L""; 
    joinParam.param.withoutloginuserJoin.vanityID = nullptr;
    joinParam.param.withoutloginuserJoin.userZAK = nullptr;

    g_pMeetingService->Join(joinParam);
}

// --- 3. LISTENERS ---
class ZoomAuthListener : public IAuthServiceEvent {
public:
    void onAuthenticationReturn(AuthResult ret) override { 
        if (ret == AUTHRET_SUCCESS) {
            JoinMeeting(); 
        }
    }
    void onLoginReturnWithReason(LOGINSTATUS ret, IAccountInfo* p, LoginFailReason r) override {}
    void onLogout() override {}
    void onZoomIdentityExpired() override {}
    void onZoomAuthIdentityExpired() override {}
    void onNotificationServiceStatus(SDKNotificationServiceStatus s, SDKNotificationServiceError e) override {}
};

class ZoomMeetingListener : public IMeetingServiceEvent {
public:
    void onMeetingStatusChanged(MeetingStatus status, int iResult) override {
        // When the meeting reaches "In Meeting" (Status 3), we subscribe to video
        if (status == MEETING_STATUS_INMEETING) {
            videoHandler = new VideoRawDataHandler();
            // This tells Zoom: "Send raw video frames to our handler"
            // Note: In a guest join, you usually need 'Allow Local Recording' 
            // from the host for this to actually trigger.
        }
    }
    void onMeetingStatisticsWarningNotification(StatisticsWarningType type) override {}
    void onMeetingParameterNotification(const MeetingParameter* p) override {}
    void onSuspendParticipantsActivities() override {}
    void onAICompanionActiveChangeNotice(bool b) override {}
    void onMeetingTopicChanged(const zchar_t* s) override {}
    void onMeetingFullToWatchLiveStream(const zchar_t* s) override {}
    void onUserNetworkStatusChanged(MeetingComponentType t, ConnectionQuality l, unsigned int u, bool up) override {}
    void onAppSignalPanelUpdated(IMeetingAppSignalHandler* p) override {}
};

// --- 4. OBS MODULE ENTRY ---
static const char* get_p_name(void* unused) { return "Zoom Participant"; }
static const char* get_s_name(void* unused) { return "Zoom Screenshare"; }
static uint32_t get_w(void* d) { return 1280; }
static uint32_t get_h(void* d) { return 720; }
static void* create_stub(obs_data_t* s, obs_source_t* src) { return (void*)1; }
static void destroy_stub(void* d) {}

struct obs_source_info z_part_info = {};
struct obs_source_info z_share_info = {}; // Placeholder for Screenshare

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    // Register Participant Source
    z_part_info.id = "zoom_participant_source";
    z_part_info.type = OBS_SOURCE_TYPE_INPUT;
    z_part_info.output_flags = OBS_SOURCE_VIDEO;
    z_part_info.get_name = get_p_name;
    z_part_info.create = create_stub;
    z_part_info.destroy = destroy_stub;
    z_part_info.get_width = get_w;
    z_part_info.get_height = get_h;
    obs_register_source(&z_part_info);

    // RESTORE SCREENSHARE IN LIST
    z_share_info.id = "zoom_screenshare_source";
    z_share_info.type = OBS_SOURCE_TYPE_INPUT;
    z_share_info.output_flags = OBS_SOURCE_VIDEO;
    z_share_info.get_name = get_s_name;
    z_share_info.create = create_stub;
    z_share_info.destroy = destroy_stub;
    z_share_info.get_width = get_w;
    z_share_info.get_height = get_h;
    obs_register_source(&z_share_info);

    InitParam initParam;
    initParam.strWebDomain = L"https://zoom.us";
    // Important: Raw Data must be enabled during Init
    initParam.rawdataOpts.enableRawdataIntermediateMode = true; 
    
    if (InitSDK(initParam) == SDKERR_SUCCESS) {
        if (CreateAuthService(&g_pAuthService) == SDKERR_SUCCESS) {
            authListener = new ZoomAuthListener();
            g_pAuthService->SetEvent(authListener);
            
            if (CreateMeetingService(&g_pMeetingService) == SDKERR_SUCCESS) {
                meetingListener = new ZoomMeetingListener();
                g_pMeetingService->SetEvent(meetingListener);
            }

            AuthContext authContext;
            authContext.jwt_token = L"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJhcHBLZXkiOiJZNzNqelFSbVF4aWhoNFo3MnFSMnRnIiwiaWF0IjoxNzc0MDUwMDAwLCJleHAiOjE3NzY2NDIwMDAsInRva2VuRXhwIjoxNzc2NjQyMDAwLCJyb2xlIjoxLCJ1c2VyRW1haWwiOiJEYXZpZEBMZXRzRG9WaWRlby5jb20ifQ.1ldmzxzK-gdzWJkxr7KkkwnYq8qEnbMGVTJFihAhuEA";
            g_pAuthService->SDKAuth(authContext);
        }
    }
    return true;
}

void obs_module_unload(void) {
    if (g_pAuthService) CleanUPSDK();
}
