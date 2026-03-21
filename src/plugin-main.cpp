#include <obs-module.h>
#include <util/platform.h>
#include <string>
#include <windows.h>

#include "zoom_sdk_def.h"
#include "zoom_sdk_raw_data_def.h"
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"

static obs_source_t* g_participantSource = nullptr;
static uint64_t g_next_timestamp = 0;

class ZoomVideoCatcher : public ZOOM_SDK_NAMESPACE::IZoomSDKRendererDelegate {
public:
    virtual void onRawDataFrameReceived(YUVRawDataI420* data) override {
        unsigned int width = data->GetStreamWidth();
        unsigned int height = data->GetStreamHeight();
        if (!g_participantSource) return;

        struct obs_source_frame obs_frame = {};
        obs_frame.format = VIDEO_FORMAT_I420; 
        obs_frame.width = width;
        obs_frame.height = height;
        obs_frame.data[0] = (uint8_t*)data->GetYBuffer();
        obs_frame.data[1] = (uint8_t*)data->GetUBuffer();
        obs_frame.data[2] = (uint8_t*)data->GetVBuffer();
        obs_frame.linesize[0] = width;
        obs_frame.linesize[1] = width / 2;
        obs_frame.linesize[2] = width / 2;
        
        video_format_get_parameters(VIDEO_CS_DEFAULT, VIDEO_RANGE_PARTIAL, obs_frame.color_matrix, obs_frame.color_range_min, obs_frame.color_range_max);
        
        uint64_t now = os_gettime_ns();
        if (g_next_timestamp == 0 || g_next_timestamp < now - 100000000ULL) g_next_timestamp = now; 
        obs_frame.timestamp = g_next_timestamp;
        g_next_timestamp += 33333333ULL;
        
        obs_source_output_video(g_participantSource, &obs_frame);
    }
    virtual void onRawDataStatusChanged(RawDataStatus status) override {}
    virtual void onRendererBeDestroyed() override {}
};

static ZoomVideoCatcher g_videoCatcher;
static ZOOM_SDK_NAMESPACE::IZoomSDKRenderer* g_videoRenderer = nullptr;

class ZoomRecordingListener : public ZOOM_SDK_NAMESPACE::IMeetingRecordingCtrlEvent {
public:
    virtual void onRecordPrivilegeChanged(bool bCanRec) override {
        if (bCanRec) {
            blog(LOG_INFO, "[Zoom to OBS] Host Status Confirmed. Starting Video Pipeline...");
            ZOOM_SDK_NAMESPACE::IMeetingService* ms = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&ms);
            if (!ms) return;

            ZOOM_SDK_NAMESPACE::IMeetingRecordingController* rec_ctrl = ms->GetMeetingRecordingController();
            if (rec_ctrl && rec_ctrl->StartRawRecording() == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
                if (ZOOM_SDK_NAMESPACE::createRenderer(&g_videoRenderer, &g_videoCatcher) == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
                    g_videoRenderer->setRawDataResolution(ZOOM_SDK_NAMESPACE::ZoomSDKResolution_720P);
                    auto part_ctrl = ms->GetMeetingParticipantsController();
                    if (part_ctrl) {
                        auto userList = part_ctrl->GetParticipantsList();
                        if (userList && userList->GetCount() > 0) {
                            g_videoRenderer->subscribe(userList->GetItem(0), ZOOM_SDK_NAMESPACE::RAW_DATA_TYPE_VIDEO);
                        }
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
#if defined(WIN32)
    virtual void onRecording2MP4Done(bool b, int r, const zchar_t* p) override {}
    virtual void onRecording2MP4Processing(int p) override {}
    virtual void onCustomizedLocalRecordingSourceNotification(ZOOM_SDK_NAMESPACE::ICustomizedLocalRecordingLayoutHelper* l) override {}
#endif
};
static ZoomRecordingListener g_recordingListener;

class ZoomMeetingListener : public ZOOM_SDK_NAMESPACE::IMeetingServiceEvent {
public:
    virtual void onMeetingStatusChanged(ZOOM_SDK_NAMESPACE::MeetingStatus status, int iResult = 0) override {
        if (status == ZOOM_SDK_NAMESPACE::MEETING_STATUS_INMEETING) {
            blog(LOG_INFO, "[Zoom to OBS] Bot is in meeting as Host.");
            ZOOM_SDK_NAMESPACE::IMeetingService* ms = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&ms);
            if (ms && ms->GetMeetingRecordingController()) {
                ms->GetMeetingRecordingController()->SetEvent(&g_recordingListener);
                // Manually trigger the privilege check since we ARE host
                g_recordingListener.onRecordPrivilegeChanged(true);
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
#if defined(WIN32)
    virtual void onAppSignalPanelUpdated(ZOOM_SDK_NAMESPACE::IMeetingAppSignalHandler* p) override {}
#endif
};
static ZoomMeetingListener g_meetingListener;

class ZoomAuthListener : public ZOOM_SDK_NAMESPACE::IAuthServiceEvent {
public:
    virtual void onAuthenticationReturn(ZOOM_SDK_NAMESPACE::AuthResult ret) override {
        if (ret == ZOOM_SDK_NAMESPACE::AUTHRET_SUCCESS) {
            blog(LOG_INFO, "[Zoom to OBS] Auth Success. Logging in...");
            ZOOM_SDK_NAMESPACE::IAuthService* auth = nullptr;
            ZOOM_SDK_NAMESPACE::CreateAuthService(&auth);
            if (auth) {
                ZOOM_SDK_NAMESPACE::LoginParam lp;
                lp.ut = ZOOM_SDK_NAMESPACE::LoginType_Email;
                lp.param.emailLogin.userName = L"YOUR_EMAIL_HERE";
                lp.param.emailLogin.password = L"YOUR_PASSWORD_HERE";
                lp.param.emailLogin.bRememberMe = true;
                auth->Login(lp);
            }
        }
    }

    virtual void onLoginReturnWithReason(ZOOM_SDK_NAMESPACE::LOGINSTATUS ret, ZOOM_SDK_NAMESPACE::IAccountInfo* pAccountInfo, ZOOM_SDK_NAMESPACE::LoginFailReason reason) override {
        if (ret == ZOOM_SDK_NAMESPACE::LOGIN_SUCCESS) {
            blog(LOG_INFO, "[Zoom to OBS] Login Success! Joining Meeting...");
            ZOOM_SDK_NAMESPACE::IMeetingService* ms = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&ms);
            if (ms) {
                ms->SetEvent(&g_meetingListener);
                ZOOM_SDK_NAMESPACE::JoinParam jp;
                jp.userType = ZOOM_SDK_NAMESPACE::SDK_UT_NORMALUSER; // Logged in user
                auto& p = jp.param.normaluserJoin;
                p.meetingNumber = 7723013754ULL;
                p.userName = L"OBS Host Bot";
                p.psw = L"";
                p.isAudioOff = true;
                p.isVideoOff = true;
                ms->Join(jp);
            }
        } else {
            blog(LOG_ERROR, "[Zoom to OBS] Login Failed. Result: %d", ret);
        }
    }

    virtual void onLogout() override {}
    virtual void onZoomIdentityExpired() override {}
    virtual void onZoomAuthIdentityExpired() override {}
#if defined(WIN32)
    virtual void onNotificationServiceStatus(ZOOM_SDK_NAMESPACE::SDKNotificationServiceStatus s, ZOOM_SDK_NAMESPACE::SDKNotificationServiceError e) override {}
#endif
};
static ZoomAuthListener g_authListener;

class ZoomSource {
public:
    obs_source_t* source;
    std::string
