#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <iostream>

// OBS Header - Mandatory for plugin recognition
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

// --- 3. OBS SOURCE DEFINITIONS ---
// These structs tell OBS that "zoom_participant_source", etc. exist.

static const char* zoom_participant_get_name(void* unused) { return "Zoom Participant"; }
static void* zoom_participant_create(obs_data_t* settings, obs_source_t* source) { return (void*)1; }
static void zoom_participant_destroy(void* data) {}

struct obs_source_info zoom_participant_info = {
    .id = "zoom_participant_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name = zoom_participant_get_name,
    .create = zoom_participant_create,
    .destroy = zoom_participant_destroy,
};

struct obs_source_info zoom_gallery_info = {
    .id = "zoom_gallery_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name = [](void*) { return "Zoom Gallery"; },
    .create = [](obs_data_t* s, obs_source_t* src) { return (void*)1; },
    .destroy = [](void* d) {},
};

struct obs_source_info zoom_screenshare_info = {
    .id = "zoom_screenshare_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO,
    .get_name = [](void*) { return "Zoom Screenshare"; },
    .create = [](obs_data_t* s, obs_source_t* src) { return (void*)1; },
    .destroy = [](void* d) {},
};

// --- 4. OBS MODULE ENTRY POINTS ---

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    // Register the sources so they are no longer "Red" in OBS
    obs_register_source(&zoom_participant_info);
    obs_register_source(&zoom_gallery_info);
    obs_register_source(&zoom_screenshare_info);

    blog(LOG_INFO, "Zoom Connector: Plugin Loaded and Sources Registered");
    return true;
}

// --- 5. SDK INITIALIZATION LOGIC ---

extern "C" __declspec(dllexport) bool InitializeSDK(const char* jwt) {
    InitParam initParam;
    initParam.strWebDomain = _T("https://zoom.us");
    
    SDKError err = InitSDK(initParam);
    if (err != SDKERR_SUCCESS) return false;

    if (CreateAuthService(&g_pAuthService) == SDKERR_SUCCESS) {
        g_pAuthService->SetEvent(&authListener);
    }

    if (CreateMeetingService(&g_pMeetingService) == SDKERR_SUCCESS) {
        g_pMeetingService->SetEvent(&meetingListener);
    }

    return true;
}
