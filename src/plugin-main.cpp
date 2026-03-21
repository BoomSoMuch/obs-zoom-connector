#include <obs-module.h>
#include <util/platform.h>
#include <string>
#include <windows.h>

// 1. Master Dictionaries FIRST
#include "zoom_sdk_def.h"
#include "zoom_sdk_raw_data_def.h"

// 2. Core Engine NEXT
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"

// 3. Meeting Components
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"

// 4. Raw Data LAST
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"

// --- [LOCKED] OBS VISUAL TARGET ---
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

// --- [LOCKED] THE RECORDING WALKIE-TALKIE ---
class ZoomRecordingListener : public ZOOM_SDK_NAMESPACE::IMeetingRecordingCtrlEvent {
public:
    virtual void onRecordPrivilegeChanged(bool bCanRec) override {
        if (bCanRec) {
            blog(LOG_INFO, "[Zoom to OBS] Host Status/Permission Confirmed!");
            
            ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
            if (!meeting_service) return;

            ZOOM_SDK_NAMESPACE::IMeetingRecordingController* rec_ctrl = meeting_service->GetMeetingRecordingController();
            if (rec_ctrl) {
                ZOOM_SDK_NAMESPACE::SDKError rec_err = rec_ctrl->StartRawRecording();
                
                if (rec_err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
                    blog(LOG_INFO, "[Zoom to OBS] Raw Recording started. Creating video pipeline...");
                    
                    ZOOM_SDK_NAMESPACE::SDKError err = ZOOM_SDK_NAMESPACE::createRenderer(&g_videoRenderer, &g_videoCatcher);
                    
                    if (err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS && g_videoRenderer) {
                        g_videoRenderer->setRawDataResolution(ZOOM_SDK_NAMESPACE::ZoomSDKResolution_720P);
                        
                        ZOOM_SDK_NAMESPACE::IMeetingParticipantsController* part_ctrl = meeting_service->GetMeetingParticipantsController();
                        if (part_ctrl) {
                            ZOOM_SDK_NAMESPACE::IList<unsigned int>* userList = part_ctrl->GetParticipantsList();
                            if (userList && userList->GetCount() > 0) {
                                unsigned int target_user_id = userList->GetItem(0);
                                err = g_videoRenderer->subscribe(target_user_id, ZOOM_SDK_NAMESPACE::RAW_DATA_TYPE_VIDEO);
                                blog(LOG_INFO, "[Zoom to OBS] Video Catcher attached to User ID %u!", target_user_id);
                            }
                        }
                    }
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

// --- [LOCKED] THE ZOOM MEETING WALKIE-TALKIE ---
class ZoomMeetingListener : public ZOOM_SDK_NAMESPACE::IMeetingServiceEvent {
public:
    virtual void onMeetingStatusChanged(ZOOM_SDK_NAMESPACE::MeetingStatus status, int iResult = 0) override {
        if (status == ZOOM_SDK_NAMESPACE::MEETING_STATUS_INMEETING) {
            blog(LOG_INFO, "[Zoom to OBS] SUCCESS! Bot joined meeting.");
            
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

// --- [LOCKED] THE ZOOM AUTH LISTENER ---
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
                param.userName = L"OBS Host Bot";
                param.psw = L""; 
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

class ZoomSource {
public:
    obs_source_t* source;
    std::string source_type;

    ZoomSource(obs_source_t* src, const std::string& source_type_name) {
        source = src;
        source_type = source_type_name;
        if (source_type == "Participant") {
            g_participantSource = source;
            blog(LOG_INFO, "[Zoom to OBS] Zoom Participant source active.");
        }
    }

    ~ZoomSource() {
        if (source_type == "Participant") {
            g_participantSource = nullptr;
        }
    }
};

void* zp_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Participant"); }
void* zs_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Screenshare"); }
void z_destroy(void* data) { delete static_cast<ZoomSource*>(data); }

struct obs_source_info zoom_participant_info = {};
struct obs_source_info zoom_screenshare_info = {};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    zoom_participant_info.get_name = [](void*) { return "Zoom Participant"; };
    zoom_participant_info.create = zp_create;
    zoom_participant_info.destroy = z_destroy;

    zoom_screenshare_info.id = "zoom_screenshare_source";
    zoom_screenshare_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_screenshare_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    zoom_screenshare_info.get_name = [](void*) { return "Zoom Screenshare"; };
    zoom_screenshare_info.create = zs_create;
    zoom_screenshare_info.destroy = z_destroy;

    obs_register_source(&zoom_participant_info);
    obs_register_source(&zoom_screenshare_info);

    ZOOM_SDK_NAMESPACE::InitParam initParam;
    initParam.strWebDomain = L"https://zoom.us";
    // [LOCKED FLAG] Required for video pipes
    initParam.rawdataOpts.enableRawdataIntermediateMode = true; 
    
    if (ZOOM_SDK_NAMESPACE::InitSDK(initParam) == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
        ZOOM_SDK_NAMESPACE::IAuthService* auth_service = nullptr;
        if (ZOOM_SDK_NAMESPACE::CreateAuthService(&auth_service) == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS && auth_service) {
            auth_service->SetEvent(&g_authListener);
            ZOOM_SDK_NAMESPACE::AuthContext authContext;
            authContext.jwt_token = L"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJhcHBLZXkiOiJZNzNqelFSbVF4aWhoNFo3MnFSMnRnIiwiaWF0IjoxNzc0MDUwMDAwLCJleHAiOjE3NzY2NDIwMDAsInRva2VuRXhwIjoxNzc2NjQyMDAwLCJyb2xlIjoxLCJ1c2VyRW1haWwiOiJEYXZpZEBMZXRzRG9WaWRlby5jb20ifQ.1ldmzxzK-gdzWJkxr7KkkwnYq8qEnbMGVTJFihAhuEA"; 
            auth_service->SDKAuth(authContext);
        }
    }
    return true;
}

void obs_module_unload(void) {}
