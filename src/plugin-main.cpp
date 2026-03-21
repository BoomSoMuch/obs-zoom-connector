#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <iostream>

// OBS Header
#include <obs-module.h>

// Zoom SDK Headers
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"

using namespace ZOOMSDK;

// --- 1. ZOOM LISTENERS ---

class ZoomAuthListener : public IAuthServiceEvent {
public:
    void onAuthenticationReturn(AuthResult ret) override { 
        blog(LOG_INFO, "[Zoom] Auth Return Code: %d", ret); 
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
        blog(LOG_INFO, "[Zoom] Meeting Status: %d", status);
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

class ZoomRecordingListener : public IMeetingRecordingCtrlEvent {
public:
    void onRecordingStatus(RecordingStatus s) override {}
    void onCloudRecordingStatus(RecordingStatus s) override {}
    void onRecordPrivilegeChanged(bool b) override {}
    void onLocalRecordingPrivilegeRequestStatus(RequestLocalRecordingStatus s) override {}
    void onRequestCloudRecordingResponse(RequestStartCloudRecordingStatus s) override {}
    void onLocalRecordingPrivilegeRequested(IRequestLocalRecordingPrivilegeHandler* h) override {}
    void onStartCloudRecordingRequested(IRequestStartCloudRecordingHandler* h) override {}
    void onCloudRecordingStorageFull(time_t g) override {}
    void onEnableAndStartSmartRecordingRequested(IRequestEnableAndStartSmartRecordingHandler* h) override {}
    void onSmartRecordingEnableActionCallback(ISmartRecordingEnableActionHandler* h) override {}
#if defined(WIN32)
    void onRecording2MP4Done(bool b, int i, const zchar_t* s) override {}
    void onRecording2MP4Processing(int i) override {}
    void onCustomizedLocalRecordingSourceNotification(ICustomizedLocalRecordingLayoutHelper* l) override {}
#endif
};

// --- 2. GLOBAL INSTANCES ---
ZoomAuthListener authListener;
ZoomMeetingListener meetingListener;
IAuthService* g_pAuthService = nullptr;
IMeetingService* g_pMeetingService = nullptr;

// --- 3. OBS SOURCE DEFINITIONS ---

static const char* get_p_name(void* unused) { return "Zoom Participant"; }
static uint32_t get_width(void* data) { return 1280; }  // Required by OBS log
static uint32_t get_height(void* data) { return 720; }  // Required by OBS log
static void* create_stub(obs_data_t* s, obs_source_t* src) { return (void*)1; }
static void destroy_stub(void* d) {}

struct obs_source_info zoom_participant_info = {};
struct obs_source_info zoom_gallery_info = {};
struct obs_source_info zoom_screenshare_info = {};

// --- 4. OBS MODULE ENTRY POINTS ---

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    // 1. Setup Source Info (Fixing the "get_width" error from your log)
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_participant_info.get_name = get_p_name;
    zoom_participant_info.create = create_stub;
    zoom_participant_info.destroy = destroy_stub;
    zoom_participant_info.get_width = get_width;
    zoom_participant_info.get_height = get_height;

    obs_register_source(&zoom_participant_info);

    // 2. Initialize Zoom SDK immediately when OBS starts
    InitParam initParam;
    initParam.strWebDomain = _T("https://zoom.us");
    
    if (InitSDK(initParam) == SDKERR_SUCCESS) {
        blog(LOG_INFO, "[Zoom] SDK Initialized");
        
        if (CreateAuthService(&g_pAuthService) == SDKERR_SUCCESS && g_pAuthService) {
            g_pAuthService->SetEvent(&authListener);
            
            // 3. START AUTHENTICATION
            // Replace "YOUR_JWT_HERE" with your actual JWT token string
            AuthContext authContext;
            authContext.jwt_token = _T("YOUR_JWT_HERE"); 
            
            if (g_pAuthService->SDKAuth(authContext) == SDKERR_SUCCESS) {
                blog(LOG_INFO, "[Zoom] Auth request sent successfully");
            }
        }
    }

    return true;
}
