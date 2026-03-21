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

// --- 1. ZOOM LISTENERS (The verified working logic) ---

class ZoomAuthListener : public IAuthServiceEvent {
public:
    void onAuthenticationReturn(AuthResult ret) override { std::cout << "Auth: " << ret << std::endl; }
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
    void onMeetingStatusChanged(MeetingStatus status, int iResult) override {}
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

// --- 3. OBS SOURCE DEFINITIONS (Fixed for C++17 compatibility) ---

static const char* get_p_name(void* unused) { return "Zoom Participant"; }
static const char* get_g_name(void* unused) { return "Zoom Gallery"; }
static const char* get_s_name(void* unused) { return "Zoom Screenshare"; }
static void* create_stub(obs_data_t* s, obs_source_t* src) { return (void*)1; }
static void destroy_stub(void* d) {}

struct obs_source_info zoom_participant_info = {};
struct obs_source_info zoom_gallery_info = {};
struct obs_source_info zoom_screenshare_info = {};

// --- 4. OBS MODULE ENTRY POINTS ---

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    // Manually assigning values to avoid Error C7555
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_participant_info.get_name = get_p_name;
    zoom_participant_info.create = create_stub;
    zoom_participant_info.destroy = destroy_stub;

    zoom_gallery_info.id = "zoom_gallery_source";
    zoom_gallery_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_gallery_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_gallery_info.get_name = get_g_name;
    zoom_gallery_info.create = create_stub;
    zoom_gallery_info.destroy = destroy_stub;

    zoom_screenshare_info.id = "zoom_screenshare_source";
    zoom_screenshare_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_screenshare_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_screenshare_info.get_name = get_s_name;
    zoom_screenshare_info.create = create_stub;
    zoom_screenshare_info.destroy = destroy_stub;

    obs_register_source(&zoom_participant_info);
    obs_register_source(&zoom_gallery_info);
    obs_register_source(&zoom_screenshare_info);

    blog(LOG_INFO, "[Zoom Connector] Loaded Successfully");
    return true;
}

// --- 5. SDK INITIALIZATION ---

extern "C" __declspec(dllexport) bool InitializeSDK(const char* jwt) {
    InitParam initParam;
    initParam.strWebDomain = _T("https://zoom.us");
    
    if (InitSDK(initParam) != SDKERR_SUCCESS) return false;

    if (CreateAuthService(&g_pAuthService) == SDKERR_SUCCESS && g_pAuthService) {
        g_pAuthService->SetEvent(&authListener);
    }

    if (CreateMeetingService(&g_pMeetingService) == SDKERR_SUCCESS && g_pMeetingService) {
        g_pMeetingService->SetEvent(&meetingListener);
    }

    return true;
}
