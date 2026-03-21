#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>
#include <iostream>

// Zoom SDK Headers - Order and pathing is critical
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"

using namespace ZOOMSDK;

// --- AUTH LISTENER ---
class ZoomAuthListener : public IAuthServiceEvent {
public:
    void onAuthenticationReturn(AuthResult ret) override { std::cout << "Auth Status: " << ret << std::endl; }
    void onLoginReturnWithReason(LOGINSTATUS ret, IAccountInfo* pAccountInfo, LoginFailReason reason) override {}
    void onLogout() override {}
    void onZoomIdentityExpired() override {}
    void onZoomAuthIdentityExpired() override {}
#if defined(WIN32)
    void onNotificationServiceStatus(SDKNotificationServiceStatus status, SDKNotificationServiceError error) override {}
#endif
};

// --- MEETING LISTENER ---
class ZoomMeetingListener : public IMeetingServiceEvent {
public:
    void onMeetingStatusChanged(MeetingStatus status, int iResult) override { std::cout << "Meeting: " << status << std::endl; }
    void onMeetingStatisticsWarningNotification(StatisticsWarningType type) override {}
    void onMeetingParameterNotification(const MeetingParameter* meeting_param) override {}
    void onSuspendParticipantsActivities() override {}
    void onAICompanionActiveChangeNotice(bool bActive) override {}
    void onMeetingTopicChanged(const zchar_t* sTopic) override {}
    void onMeetingFullToWatchLiveStream(const zchar_t* sLiveStreamUrl) override {}
    void onUserNetworkStatusChanged(MeetingComponentType type, ConnectionQuality level, unsigned int userId, bool uplink) override {}
#if defined(WIN32)
    void onAppSignalPanelUpdated(IMeetingAppSignalHandler* pHandler) override {}
#endif
};

// --- RECORDING LISTENER ---
class ZoomRecordingListener : public IMeetingRecordingCtrlEvent {
public:
    // Core Overrides
    void onRecordingStatus(RecordingStatus status) override {}
    void onCloudRecordingStatus(RecordingStatus status) override {}
    void onRecordPrivilegeChanged(bool bCanRec) override {}
    void onLocalRecordingPrivilegeRequestStatus(RequestLocalRecordingStatus status) override {}
    void onRequestCloudRecordingResponse(RequestStartCloudRecordingStatus status) override {}
    void onLocalRecordingPrivilegeRequested(IRequestLocalRecordingPrivilegeHandler* handler) override {}
    void onStartCloudRecordingRequested(IRequestStartCloudRecordingHandler* handler) override {}
    void onCloudRecordingStorageFull(time_t gracePeriodDate) override {}
    
    // Smart Recording Overrides (Found in your latest file)
    void onEnableAndStartSmartRecordingRequested(IRequestEnableAndStartSmartRecordingHandler* handler) override {}
    void onSmartRecordingEnableActionCallback(ISmartRecordingEnableActionHandler* handler) override {}

#if defined(WIN32)
    // Windows Transcoding Overrides
    void onRecording2MP4Done(bool bsuccess, int iResult, const zchar_t* szPath) override {}
    void onRecording2MP4Processing(int iPercentage) override {}
    void onCustomizedLocalRecordingSourceNotification(ICustomizedLocalRecordingLayoutHelper* layout_helper) override {}
#endif
};

// Global Service Pointers and Listeners
ZoomAuthListener authListener;
ZoomMeetingListener meetingListener;
ZoomRecordingListener recordingListener;

IAuthService* g_pAuthService = nullptr;
IMeetingService* g_pMeetingService = nullptr;

extern "C" __declspec(dllexport) bool InitializeSDK(const char* jwt) {
    InitParam initParam;
    initParam.strWebDomain = _T("https://zoom.us");
    
    // 1. Direct call to global Init (as per your zoom_sdk.h)
    SDKError err = InitSDK(initParam);
    if (err != SDKERR_SUCCESS) return false;

    // 2. Create services using global factory functions
    err = CreateAuthService(&g_pAuthService);
    if (err == SDKERR_SUCCESS && g_pAuthService) {
        g_pAuthService->SetEvent(&authListener);
    }

    err = CreateMeetingService(&g_pMeetingService);
    if (err == SDKERR_SUCCESS && g_pMeetingService) {
        g_pMeetingService->SetEvent(&meetingListener);
    }

    return true;
}
