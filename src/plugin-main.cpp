#include <obs-module.h>
#include <util/platform.h>
#include <string>
#include <vector>
#include <windows.h>

// 1. Master Dictionaries (VERIFIED)
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

// --- [LOCKED] THE RECORDING LISTENER ---
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

// --- [LOCKED] MEETING AND AUTH LISTENERS ---
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
                param.userName = L"OBS Host Bot";
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

// --- NEW: THE PARTICIPANT CONTROLLER SOURCE ---
class ZoomSource {
public:
    obs_source_t* source;
    unsigned int current_user_id = 0;

    ZoomSource(obs_source_t* src) : source(src) {
        g_participantSource = source;
    }
    ~ZoomSource() {
        if (g_participantSource == source) g_participantSource = nullptr;
    }

    void update(obs_data_t* settings) {
        unsigned int selected_id = (unsigned int)obs_data_get_int(settings, "participant_id");
        if (selected_id != current_user_id && g_videoRenderer) {
            current_user_id = selected_id;
            g_videoRenderer->unSubscribe();
            if (current_user_id != 0) {
                g_videoRenderer->subscribe(current_user_id, ZOOM_SDK_NAMESPACE::RAW_DATA_TYPE_VIDEO);
                blog(LOG_INFO, "[Zoom to OBS] Switching to User ID: %u", current_user_id);
            }
        }
    }
};

static obs_properties_t* zp_properties(void* data) {
    obs_properties_t* props = obs_properties_create();
    obs_property_t* list = obs_properties_add_list(props, "participant_id", "Zoom Participant", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    
    obs_property_list_add_int(list, "--- Select User ---", 0);

    ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
    ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
    if (meeting_service) {
        // VERIFIED: Correct method name for 6.7.5 headers
        ZOOM_SDK_NAMESPACE::IMeetingParticipantsController* part_ctrl = meeting_service->GetMeetingParticipantsController();
        if (part_ctrl) {
            ZOOM_SDK_NAMESPACE::IList<unsigned int>* userList = part_ctrl->GetParticipantsList();
            if (userList) {
                for (int i = 0; i < userList->GetCount(); i++) {
                    unsigned int uid = userList->GetItem(i);
                    ZOOM_SDK_NAMESPACE::IUserInfo* info = part_ctrl->GetUserByUserID(uid);
                    if (info) {
                        // VERIFIED: Wide to Multi-byte conversion for OBS UI
                        std::wstring wname = info->GetUserName();
                        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wname[0], (int)wname.size(), NULL, 0, NULL, NULL);
                        std::string name(size_needed, 0);
                        WideCharToMultiByte(CP_UTF8, 0, &wname[0], (int)wname.size(), &name[0], size_needed, NULL, NULL);
                        obs_property_list_add_int(list, name.c_str(), (long long)uid);
                    }
                }
            }
        }
    }
    return props;
}

void* zp_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source); }
void z_destroy(void* data) { delete static_cast<ZoomSource*>(data); }
void zp_update(void* data, obs_data_t* settings) { static_cast<ZoomSource*>(data)->update(settings); }

struct obs_source_info zoom_participant_info = {};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
    zoom_participant_info.get_name = [](void*) { return "Zoom Participant"; };
    zoom_participant_info.create = zp_create;
    zoom_participant_info.destroy = z_destroy;
    zoom_participant_info.get_properties = zp_properties;
    zoom_participant_info.update = zp_update;

    obs_register_source(&zoom_participant_info);

    ZOOM_SDK_NAMESPACE::InitParam initParam;
    initParam.strWebDomain = L"https://zoom.us";
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
