#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <obs-module.h>
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "zoom_sdk_raw_data_def.h"
#include "rawdata/rawdata_renderer_interface.h"

using namespace ZOOMSDK;

// --- 1. GLOBAL INSTANCES ---
class ZoomAuthListener;
class ZoomMeetingListener;
class ZoomVideoCatcher;

ZoomAuthListener* g_authListener = nullptr;
ZoomMeetingListener* g_meetingListener = nullptr;
IZoomSDKRenderer* g_videoRenderer = nullptr;
IAuthService* g_pAuthService = nullptr;
IMeetingService* g_pMeetingService = nullptr;
obs_source_t* g_participantSource = nullptr;

// --- 2. VIDEO CATCHER (The Celebrated Version) ---
class ZoomVideoCatcher : public IZoomSDKRendererDelegate {
public:
    void onRawDataFrameReceived(YUVRawDataI420* data) override {
        if (!g_participantSource) return;

        struct obs_source_frame obs_frame = {};
        obs_frame.format = VIDEO_FORMAT_I420;
        obs_frame.width = data->GetStreamWidth();
        obs_frame.height = data->GetStreamHeight();
        obs_frame.data[0] = (uint8_t*)data->GetYBuffer();
        obs_frame.data[1] = (uint8_t*)data->GetUBuffer();
        obs_frame.data[2] = (uint8_t*)data->GetVBuffer();
        obs_frame.linesize[0] = data->GetStreamWidth();
        obs_frame.linesize[1] = data->GetStreamWidth() / 2;
        obs_frame.linesize[2] = data->GetStreamWidth() / 2;
        obs_frame.timestamp = os_gettime_ns();

        obs_source_output_video(g_participantSource, &obs_frame);
    }
    void onRawDataStatusChanged(RawDataStatus status) override {}
    void onRendererBeDestroyed() override {}
};
static ZoomVideoCatcher g_videoDelegate;

// --- 3. LISTENERS ---
class ZoomAuthListener : public IAuthServiceEvent {
public:
    void onAuthenticationReturn(AuthResult ret) override {
        if (ret == AUTHRET_SUCCESS) {
            JoinParam joinParam;
            joinParam.userType = SDK_UT_WITHOUT_LOGIN;
            joinParam.param.withoutloginuserJoin.meetingNumber = 7723013754;
            joinParam.param.withoutloginuserJoin.userName = L"OBS Connector";
            g_pMeetingService->Join(joinParam);
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
        if (status == MEETING_STATUS_INMEETING) {
            blog(LOG_INFO, "[Zoom] Joined Meeting. Initializing Video Pipe...");
            IMeetingRecordingController* rec_ctrl = g_pMeetingService->GetMeetingRecordingController();
            if (rec_ctrl && rec_ctrl->StartRawRecording() == SDKERR_SUCCESS) {
                if (SDKERR_SUCCESS == createRenderer(&g_videoRenderer, &g_videoDelegate)) {
                    IMeetingParticipantsController* pCtrl = g_pMeetingService->GetMeetingParticipantsController();
                    if (pCtrl) {
                        IList<unsigned int>* pList = pCtrl->GetParticipantsList();
                        if (pList && pList->GetCount() > 0) {
                            g_videoRenderer->subscribe(pList->GetItem(0), RAW_DATA_TYPE_VIDEO);
                        }
                    }
                }
            }
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

// --- 4. OBS SOURCE HELPERS ---
static void* zp_create(obs_data_t* settings, obs_source_t* source) {
    g_participantSource = source;
    return (void*)1;
}
static void zp_destroy(void* data) { g_participantSource = nullptr; }

// --- 5. OBS MODULE ENTRY ---
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    struct obs_source_info p_info = {};
    p_info.id = "zoom_participant_source";
    p_info.type = OBS_SOURCE_TYPE_INPUT;
    p_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    p_info.get_name = [](void*) { return "Zoom Participant"; };
    p_info.create = zp_create;
    p_info.destroy = zp_destroy;
    obs_register_source(&p_info);

    // Placeholder Screenshare Source
    struct obs_source_info s_info = {};
    s_info.id = "zoom_screenshare_source";
    s_info.type = OBS_SOURCE_TYPE_INPUT;
    s_info.get_name = [](void*) { return "Zoom Screenshare"; };
    s_info.create = [](obs_data_t* s, obs_source_t* src) { return (void*)1; };
    obs_register_source(&s_info);

    InitParam initParam;
    initParam.strWebDomain = L"https://zoom.us";
    initParam.rawdataOpts.enableRawdataIntermediateMode = true; // CRITICAL FOR VIDEO

    if (InitSDK(initParam) == SDKERR_SUCCESS) {
        if (CreateAuthService(&g_pAuthService) == SDKERR_SUCCESS) {
            g_authListener = new ZoomAuthListener();
            g_pAuthService->SetEvent(g_authListener);
            
            if (CreateMeetingService(&g_pMeetingService) == SDKERR_SUCCESS) {
                g_meetingListener = new ZoomMeetingListener();
                g_pMeetingService->SetEvent(g_meetingListener);
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
