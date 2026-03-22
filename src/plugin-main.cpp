#include <obs-module.h>
#include <util/platform.h>
#include <string>
#include <vector>
#include <windows.h>

// 1. Master Dictionaries
#include "zoom_sdk_def.h"
#include "zoom_sdk_raw_data_def.h"

// 2. Core Engine
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"

// 3. Meeting Components
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"

// 4. Raw Data
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"

// --- [LOCKED] THE OBS VISUAL TARGET ---
static obs_source_t* g_participantSource = nullptr;
static uint64_t g_next_timestamp = 0;

// --- [LOCKED] THE ZOOM VIDEO CATCHER ---
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
        
        video_format_get_parameters(VIDEO_CS_DEFAULT, VIDEO_RANGE_PARTIAL, 
                                    obs_frame.color_matrix, 
                                    obs_frame.color_range_min, 
                                    obs_frame.color_range_max);
        
        uint64_t now = os_gettime_ns();
        if (g_next_timestamp == 0 || g_next_timestamp < now - 100000000ULL) {
            g_next_timestamp = now; 
        }
        obs_frame.timestamp = g_next_timestamp;
        g_next_timestamp += 33333333ULL; 
        obs_source_output_video(g_participantSource, &obs_frame);
    }
    virtual void onRawDataStatusChanged(RawDataStatus status) override {}
    virtual void onRendererBeDestroyed() override {}
};

static ZoomVideoCatcher g_videoCatcher;
static ZOOM_SDK_NAMESPACE::IZoomSDKRenderer* g_videoRenderer = nullptr;

// --- [LOCKED] RECORDING & AUTH LISTENERS ---
class ZoomRecordingListener : public ZOOM_SDK_NAMESPACE::IMeetingRecordingCtrlEvent {
public:
    virtual void onRecordPrivilegeChanged(bool bCanRec) override {
        if (bCanRec) {
            ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
            if (!meeting_service) return;
            ZOOM_SDK_NAMESPACE::IMeetingRecordingController* rec_ctrl = meeting_service->GetMeetingRecordingController();
            if (rec_ctrl) {
                rec_ctrl->StartRawRecording();
                if (!g_videoRenderer) {
                    ZOOM_SDK_NAMESPACE::createRenderer(&g_videoRenderer, &g_videoCatcher);
                    if (g_videoRenderer) g_videoRenderer->setRawDataResolution(ZOOM_SDK_NAMESPACE::ZoomSDKResolution_720P);
                }
            }
        }
    }
    virtual void onRecordingStatus(ZOOM_SDK_NAMESPACE::RecordingStatus status) override {}
    virtual void onCloudRecordingStatus(ZOOM_SDK_NAMESPACE::RecordingStatus status) override {}
    virtual void onLocalRecordingPrivilegeRequestStatus(ZOOM_SDK_NAMESPACE::RequestLocalRecordingStatus status) override {}
    virtual void onRequestCloudRecordingResponse(ZOOM_SDK_NAMESPACE::RequestStartCloudRecordingStatus status) override {}
    virtual void onLocalRecordingPrivilegeRequested(ZOOM_SDK_NAMESPACE::IRequestLocalRecordingPrivilegeHandler* handler) override {}
    virtual void onStartCloudRecordingRequested(ZOOM_SDK_NAMESPACE::IRequestStartCloudRecordingHandler* handler) override {}
    virtual void onCloudRecordingStorageFull(time_t gracePeriodDate) override {}
    virtual void onEnableAndStartSmartRecordingRequested(ZOOM_SDK_NAMESPACE::IRequestEnableAndStartSmartRecordingHandler* handler) override {}
    virtual void onSmartRecordingEnableActionCallback(ZOOM_SDK_NAMESPACE::ISmartRecordingEnableActionHandler* handler) override {}
#if defined(WIN32)
    virtual void onRecording2MP4Done(bool bsuccess, int iResult, const zchar_t* szPath) override {}
    virtual void onRecording2MP4Processing(int iPercentage) override {}
    virtual void onCustomizedLocalRecordingSourceNotification(ZOOM_SDK_NAMESPACE::ICustomizedLocalRecordingLayoutHelper* layout_helper) override {}
#endif
};
static ZoomRecordingListener g_recordingListener;

class ZoomMeetingListener : public ZOOM_SDK_NAMESPACE::IMeetingServiceEvent {
public:
    virtual void onMeetingStatusChanged(ZOOM_SDK_NAMESPACE::MeetingStatus status, int iResult = 0) override {
        if (status == ZOOM_SDK_NAMESPACE::MEETING_STATUS_INMEETING) {
            ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
            if (meeting_service) {
                ZOOM_SDK_NAMESPACE::IMeetingRecordingController* rec_ctrl = meeting_service->GetMeetingRecordingController();
                if (rec_ctrl) {
                    rec_ctrl->SetEvent(&g_recordingListener);
                    g_recordingListener.onRecordPrivilegeChanged(true);
                }
            }
        }
    }
    virtual void onMeetingStatisticsWarningNotification(ZOOM_SDK_NAMESPACE::StatisticsWarningType type) override {}
    virtual void onMeetingParameterNotification(const ZOOM_SDK_NAMESPACE::MeetingParameter* meeting_param) override {}
    virtual void onSuspendParticipantsActivities() override {}
    virtual void onAICompanionActiveChangeNotice(bool bActive) override {}
    virtual void onMeetingTopicChanged(const zchar_t* sTopic) override {}
    virtual void onMeetingFullToWatchLiveStream(const zchar_t* sLiveStreamUrl) override {}
    virtual void onUserNetworkStatusChanged(ZOOM_SDK_NAMESPACE::MeetingComponentType type, ZOOM_SDK_NAMESPACE::ConnectionQuality level, unsigned int userId, bool uplink) override {}
#if defined(WIN32)
    virtual void onAppSignalPanelUpdated(ZOOM_SDK_NAMESPACE::IMeetingAppSignalHandler* pHandler) override {}
#endif
};
static ZoomMeetingListener g_meetingListener;

class ZoomAuthListener : public ZOOM_SDK_NAMESPACE::IAuthServiceEvent {
public:
    virtual void onAuthenticationReturn(ZOOM_SDK_NAMESPACE::AuthResult ret) override {
        if (ret == ZOOM_SDK_NAMESPACE::AUTHRET_SUCCESS) {
            ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
            if (meeting_service) {
                meeting_service->SetEvent(&g_meetingListener);
                ZOOM_SDK_NAMESPACE::JoinParam joinParam;
                joinParam.userType = ZOOM_SDK_NAMESPACE::SDK_UT_WITHOUT_LOGIN;
                ZOOM_SDK_NAMESPACE::JoinParam4WithoutLogin& param = joinParam.param.withoutloginuserJoin;
                param.meetingNumber = 7723013754ULL; 
                param.userName = L"ISO for OBS"; 
                param.isAudioOff = true;
                param.isVideoOff = true;
                meeting_service->Join(joinParam);
            }
        }
    }
    virtual void onLoginReturnWithReason(ZOOM_SDK_NAMESPACE::LOGINSTATUS ret, ZOOM_SDK_NAMESPACE::IAccountInfo* pAccountInfo, ZOOM_SDK_NAMESPACE::LoginFailReason reason) override {}
    virtual void onLogout() override {}
    virtual void onZoomIdentityExpired() override {}
    virtual void onZoomAuthIdentityExpired() override {}
#if defined(WIN32)
    virtual void onNotificationServiceStatus(ZOOM_SDK_NAMESPACE::SDKNotificationServiceStatus status, ZOOM_SDK_NAMESPACE::SDKNotificationServiceError error) override {}
#endif
};
static ZoomAuthListener g_authListener;

// --- SOURCE CLASSES ---
class ZoomParticipantSource {
public:
    obs_source_t* source;
    unsigned int current_user_id = 0;
    ZoomParticipantSource(obs_source_t* src) : source(src) { g_participantSource = source; }
    ~ZoomParticipantSource() { if (g_participantSource == source) g_participantSource = nullptr; }
    void update(obs_data_t* settings) {
        unsigned int selected_id = (unsigned int)obs_data_get_int(settings, "participant_id");
        if (selected_id != current_user_id && g_videoRenderer) {
            current_user_id = selected_id;
            g_videoRenderer->unSubscribe();
            if (current_user_id != 0) g_videoRenderer->subscribe(current_user_id, ZOOM_SDK_NAMESPACE::RAW_DATA_TYPE_VIDEO);
        }
    }
};

class ZoomScreenshareSource {
public:
    obs_source_t* source;
    ZoomScreenshareSource(obs_source_t* src) : source(src) {}
};

// --- PROPERTIES (Verified Logic) ---
static obs_properties_t* zp_properties(void* data) {
    obs_properties_t* props = obs_properties_create();
    
    // Alpha Label
    obs_properties_add_text(props, "ver_label", "ISO for OBS (v0.1 Alpha)", OBS_TEXT_INFO);

    // Active Status via IMeetingInfo
    std::string status_text = "Status: Disconnected";
    ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
    ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
    if (meeting_service) {
        if (meeting_service->GetMeetingStatus() == ZOOM_SDK_NAMESPACE::MEETING_STATUS_INMEETING) {
            ZOOM_SDK_NAMESPACE::IMeetingInfo* info = meeting_service->GetMeetingInfo();
            if (info) status_text = "Status: Connected to Meeting " + std::to_string(info->GetMeetingNumber());
        }
    }
    obs_properties_add_text(props, "status_label", status_text.c_str(), OBS_TEXT_INFO);

    obs_property_t* list = obs_properties_add_list(props, "participant_id", "Select Participant", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(list, "--- Select User ---", 0);
    obs_property_list_add_int(list, "[Active Speaker]", 1); 

    if (meeting_service) {
        ZOOM_SDK_NAMESPACE::IMeetingParticipantsController* part_ctrl = meeting_service->GetMeetingParticipantsController();
        if (part_ctrl) {
            ZOOM_SDK_NAMESPACE::IList<unsigned int>* userList = part_ctrl->GetParticipantsList();
            if (userList) {
                for (int i = 0; i < userList->GetCount(); i++) {
                    unsigned int uid = userList->GetItem(i);
                    ZOOM_SDK_NAMESPACE::IUserInfo* info = part_ctrl->GetUserByUserID(uid);
                    if (info) {
                        std::wstring wname = info->GetUserName();
                    if (wname == L"ISO for OBS") continue;
                        
                        // FILTER: Hide the bot
