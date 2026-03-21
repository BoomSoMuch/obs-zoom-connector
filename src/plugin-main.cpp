// 1. Windows system headers MUST be first
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tchar.h>

// 2. Zoom SDK headers (Corrected paths based on your folder structure)
#include "zoom_sdk.h"
#include "meeting_service_interface.h"
#include "auth_service_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"

#include <iostream>

using namespace ZOOMSDK;

// --- AUTH LISTENER ---
class ZoomAuthListener : public IAuthServiceEvent {
public:
    void onAuthenticationReturn(AuthResult ret) override { std::cout << "Auth: " << ret << std::endl; }
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
    void onMeetingStatusChanged(MeetingStatus status, int iResult) override { std::cout << "Status: " << status << std::endl; }
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
    void onRecordingStatus(RecordingStatus status) override {}
    void onCloudRecordingStatus(RecordingStatus status) override { std::cout << "Cloud Rec Status: " << status << std::endl; }
    void onRecordPrivilegeChanged(bool bCanRec) override {}
    void onLocalRecordingPrivilegeRequestStatus(RequestLocalRecordingStatus status) override {}
    void onRequestCloudRecordingResponse(RequestStartCloudRecordingStatus status) override {}
    void onLocalRecordingPrivilegeRequested(IRequestLocalRecordingPrivilegeHandler* handler) override {}
    void onStartCloudRecordingRequested(IRequestStartCloudRecordingHandler* handler) override {}
    void onCloudRecordingStorageFull(time_t gracePeriodDate) override {}
    void onEnableAndStartSmartRecordingRequested(IRequestEnableAndStartSmartRecordingHandler* handler) override {}
    void onSmartRecordingEnableActionCallback(ISmartRecordingEnableActionHandler* handler) override {}
#if defined(WIN32)
    void onRecording2MP4Done(bool bsuccess, int iResult, const zchar_t* szPath) override {}
    void onRecording2MP4Processing(int iPercentage) override {}
    void onCustomizedLocalRecordingSourceNotification(ICustomizedLocalRecordingLayoutHelper* layout_helper) override {}
#endif
};

// Global Listener Instances
ZoomAuthListener authListener;
ZoomMeetingListener meetingListener;
ZoomRecordingListener recordingListener;

// Global SDK Pointer
IZoomSDK* g_pSDKInst = nullptr; 

extern "C" __declspec(dllexport) bool InitializeSDK(const char* jwt) {
    InitParam initParam;
    initParam.strWebDomain = _T("https://zoom.us");
    
    // Call the SDK-provided function to create the instance
    SDKError err = CreateSDKInst(&g_pSDKInst);
    if (err != SDKERR_SUCCESS || !g_pSDKInst) return false;

    err = g_pSDKInst->InitSDK(initParam);
    if (err != SDKERR_SUCCESS) return false;

    IAuthService* pAuth = g_pSDKInst->GetAuthService();
    if (pAuth) pAuth->SetEvent(&authListener);

    IMeetingService* pMeeting = g_pSDKInst->GetMeetingService();
    if (pMeeting) pMeeting->SetEvent(&meetingListener);

    return true;
}
