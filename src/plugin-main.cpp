#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <obs-module.h>
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"

using namespace ZOOMSDK;

// --- 1. GLOBAL INSTANCES ---
class ZoomAuthListener;
class ZoomMeetingListener;
ZoomAuthListener* authListener = nullptr;
ZoomMeetingListener* meetingListener = nullptr;
IAuthService* g_pAuthService = nullptr;
IMeetingService* g_pMeetingService = nullptr;

// --- 2. JOIN MEETING LOGIC ---
void JoinMeeting() {
    if (!g_pMeetingService) return;

    JoinParam joinParam;
    // Matching the member name from your meeting service header
    joinParam.userType = SDK_UT_WITHOUT_LOGIN;
    joinParam.param.withoutloginuserJoin.meetingNumber = 7723013754;
    joinParam.param.withoutloginuserJoin.userName = L"OBS Connector";
    joinParam.param.withoutloginuserJoin.psw = L""; 
    joinParam.param.withoutloginuserJoin.vanityID = nullptr;
    joinParam.param.withoutloginuserJoin.userZAK = nullptr;

    SDKError err = g_pMeetingService->Join(joinParam);
    blog(LOG_INFO, "[Zoom] Join Meeting Attempt for 7723013754: %d", err);
}

// --- 3. LISTENERS ---
class ZoomAuthListener : public IAuthServiceEvent {
public:
    void onAuthenticationReturn(AuthResult ret) override { 
        blog(LOG_INFO, "[Zoom] Auth Return Code: %d", ret); 
        if (ret == AUTHRET_SUCCESS) {
            blog(LOG_INFO, "[Zoom] Auth Successful! Joining meeting...");
            JoinMeeting(); 
        }
    }
    void onLoginReturnWithReason(LOGINSTATUS ret, IAccountInfo* p, LoginFailReason r) override {}
    void onLogout() override {}
    void onZoomIdentityExpired() override {}
    void onZoomAuthIdentityExpired() override {}
#if defined(WIN32)
    void onNotificationServiceStatus(SDKNotificationServiceStatus s, SDKNotificationServiceError e) override {}
#endif
};

class ZoomMeetingListener : public IMeetingServiceEvent {
public:
    void onMeetingStatusChanged(MeetingStatus status, int iResult) override {
        blog(LOG_INFO, "[Zoom] Meeting Status Changed: %d", status);
    }
    void onMeetingStatisticsWarningNotification(StatisticsWarningType type) override {}
    void onMeetingParameterNotification(const MeetingParameter* p) override {}
    void onSuspendParticipantsActivities() override {}
    void onAICompanionActiveChangeNotice(bool b) override {}
    void onMeetingTopicChanged(const zchar_t* s) override {}
    void onMeetingFullToWatchLiveStream(const zchar_t* s) override {}
    void onUserNetworkStatusChanged(MeetingComponentType t, ConnectionQuality l, unsigned int u, bool up) override {}
#if defined(WIN32)
    void onAppSignalPanelUpdated(IMeetingAppSignalHandler* p) override {}
#endif
};

// --- 4. OBS MODULE ENTRY ---
static const char* get_p_name(void* unused) { return "Zoom Participant"; }
static uint32_t get_w(void* d) { return 1280; }
static uint32_t get_h(void* d) { return 720; }
static void* create_stub(obs_data_t* s, obs_source_t* src) { return (void*)1; }
static void destroy_stub(void* d) {}

struct obs_source_info z_part_info = {};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    z_part_info.id = "zoom_participant_source";
    z_part_info.type = OBS_SOURCE_TYPE_INPUT;
    z_part_info.output_flags = OBS_SOURCE_VIDEO;
    z_part_info.get_name = get_p_name;
    z_part_info.create = create_stub;
    z_part_info.destroy = destroy_stub;
    z_part_info.get_width = get_w;
    z_part_info.get_height = get_h;
    obs_register_source(&z_part_info);

    InitParam initParam;
    initParam.strWebDomain = L"https://zoom.us";
    
    if (InitSDK(initParam) == SDKERR_SUCCESS) {
        blog(LOG_INFO, "[Zoom] SDK Initialized.");
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
            blog(LOG_INFO, "[Zoom] Auth request sent.");
        }
    }
    return true;
}

void obs_module_unload(void) {
    // Corrected function name from zoom_sdk.h
    if (g_pAuthService) CleanUPSDK(); 
}
