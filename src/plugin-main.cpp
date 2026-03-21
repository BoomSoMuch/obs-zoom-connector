#include "zoom_sdk.h"
#include "meeting_service_interface.h"
#include "auth_service_interface.h"
// Corrected path based on your 'meeting_service_components' folder
#include "meeting_service_components/meeting_recording_interface.h"
#include <iostream>
#include <tchar.h> // For _T() support

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
    // Mandatory for Windows interface
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
    // Added missing network status handler
    void onUserNetworkStatusChanged(MeetingComponentType type, ConnectionQuality level, unsigned int userId, bool uplink) override {}
#if defined(WIN32)
    // Mandatory Windows callback
    void onAppSignalPanelUpdated(IMeetingAppSignalHandler* pHandler) override {}
#endif
};

// --- RECORDING LISTENER ---
class ZoomRecordingListener : public IMeetingRecordingCtrlEvent {
public:
    void onRecordingStatus(RecordingStatus status) override {}
    void onCloudRecordingStatus(RecordingStatus status) override {}
    void onRecordPrivilegeChanged(bool bCanRec) override {}
    void onLocalRecordingPrivilegeRequestStatus(RequestLocalRecordingStatus status) override {}
    void onRequestCloudRecordingResponse(RequestStartCloudRecordingStatus status) override {}
    void onLocalRecordingPrivilegeRequested(IRequestLocalRecordingPrivilegeHandler* handler) override {}
    void onStartCloudRecordingRequested(IRequestStartCloudRecordingHandler* handler) override {}
    void onCloudRecordingStorageFull(time_t gracePeriodDate) override {}
    void onEnableAndStartSmartRecordingRequested(IRequestEnableAndStartSmartRecordingHandler* handler) override {}
    void onSmartRecordingEnableActionCallback(ISmartRecordingEnableActionHandler* handler) override {}
#if defined(WIN32)
    // Mandatory Windows callbacks for local recording
    void onRecording2MP4Done(bool bsuccess, int iResult, const zchar_t* szPath) override {}
    void onRecording2MP4Processing(int iPercentage) override {}
    void onCustomizedLocalRecordingSourceNotification(ICustomizedLocalRecordingLayoutHelper* layout_helper) override {}
#endif
};

// Global instances
ZoomAuthListener authListener;
ZoomMeetingListener meetingListener;
ZoomRecordingListener recordingListener;

extern "C" __declspec(dllexport) bool InitializeSDK(const char* jwt) {
    InitParam initParam;
    initParam.strWebDomain = _T("https://zoom.us");
    
    IZoomSDK* SDKInst = nullptr;
    SDKError err = CreateSDKInst(&SDKInst);
    if (err != SDKERR_SUCCESS) return false;

    err = SDKInst->InitSDK(initParam);
    if (err != SDKERR_SUCCESS) return false;

    IAuthService* pAuth = SDKInst->GetAuthService();
    if (pAuth) pAuth->SetEvent(&authListener);

    IMeetingService* pMeeting = SDKInst->GetMeetingService();
    if (pMeeting) pMeeting->SetEvent(&meetingListener);

    return true;
}
