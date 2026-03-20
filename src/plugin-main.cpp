extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}
#include <obs-module.h>
#include <util/platform.h>
#include <ixwebsocket/IXWebSocket.h>
#include <string>
#include <windows.h>

// 1. Master Dictionaries FIRST
#include "zoom_sdk_def.h"
#include "zoom_sdk_raw_data_def.h"

// 2. Core Engine NEXT
#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"

// 3. Meeting Components (AUDIO MUST GO BEFORE PARTICIPANTS!)
#include "meeting_service_components/meeting_audio_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"
#include "meeting_service_components/meeting_recording_interface.h" // <-- ADDED FOR RECORDING PERMISSIONS

// 4. Raw Data LAST
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"

// --- 3. THE ZOOM VIDEO CATCHER ---
class ZoomVideoCatcher : public ZOOM_SDK_NAMESPACE::IZoomSDKRendererDelegate {
public:
    virtual void onRawDataFrameReceived(YUVRawDataI420* data) override {
        unsigned int width = data->GetStreamWidth();
        unsigned int height = data->GetStreamHeight();
        
        // Throttle the log so we don't spam OBS to death
        static int frameCount = 0;
        if (frameCount % 300 == 0) { 
            blog(LOG_INFO, "[Zoom to OBS] CATCHER ALERT: Received Video Frame! Resolution: %dx%d", width, height);
        }
        frameCount++;
    }

    virtual void onRawDataStatusChanged(RawDataStatus status) override {
        blog(LOG_INFO, "[Zoom to OBS] Video Raw Data Status Changed: %d", status);
    }

    virtual void onRendererBeDestroyed() override {
        blog(LOG_INFO, "[Zoom to OBS] Video Renderer Destroyed.");
    }
};

// Create one global instance of our catcher
static ZoomVideoCatcher g_videoCatcher;

// Create a global pointer to hold the pipeline open
static ZOOM_SDK_NAMESPACE::IZoomSDKRenderer* g_videoRenderer = nullptr;
// ---------------------------------

// --- 4. THE RECORDING WALKIE-TALKIE ---
class ZoomRecordingListener : public ZOOM_SDK_NAMESPACE::IMeetingRecordingCtrlEvent {
public:
    virtual void onRecordPrivilegeChanged(bool bCanRec) override {
        if (bCanRec) {
            blog(LOG_INFO, "[Zoom to OBS] BOOM! Host granted recording permission!");
            
            ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
            if (!meeting_service) return;

            ZOOM_SDK_NAMESPACE::IMeetingRecordingController* rec_ctrl = meeting_service->GetMeetingRecordingController();
            if (rec_ctrl) {
                // Zoom requires us to formally start a "raw recording" before stealing pixels
                ZOOM_SDK_NAMESPACE::SDKError rec_err = rec_ctrl->StartRawRecording();
                
                if (rec_err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
                    blog(LOG_INFO, "[Zoom to OBS] Raw Recording started. Creating video pipeline...");
                    
                    // --- PHASE 3: CREATE THE VIDEO PIPELINE ---
                    ZOOM_SDK_NAMESPACE::SDKError err = ZOOM_SDK_NAMESPACE::createRenderer(&g_videoRenderer, &g_videoCatcher);
                    
                    if (err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS && g_videoRenderer) {
                        g_videoRenderer->setRawDataResolution(ZOOM_SDK_NAMESPACE::ZoomSDKResolution_720P);
                        
                        ZOOM_SDK_NAMESPACE::IMeetingParticipantsController* part_ctrl = meeting_service->GetMeetingParticipantsController();
                        if (part_ctrl) {
                            ZOOM_SDK_NAMESPACE::IList<unsigned int>* userList = part_ctrl->GetParticipantsList();
                            if (userList && userList->GetCount() > 0) {
                                // Grab the User ID of the very first person in the room
                                unsigned int target_user_id = userList->GetItem(0);
                                
                                err = g_videoRenderer->subscribe(target_user_id, ZOOM_SDK_NAMESPACE::RAW_DATA_TYPE_VIDEO);
                                blog(LOG_INFO, "[Zoom to OBS] Video Catcher attached to User ID %u! Subscribe result: %d", target_user_id, err);
                            }
                        }
                    } else {
                        blog(LOG_ERROR, "[Zoom to OBS] ERROR: Failed to create video renderer. Code: %d", err);
                    }
                } else {
                    blog(LOG_ERROR, "[Zoom to OBS] ERROR: Failed to start raw recording. Code: %d", rec_err);
                }
            }
        }
    }

    // Required overrides (Mathematically verified against your specific dictionary)
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

#if defined(__linux__)
    virtual void onTranscodingStatusChanged(ZOOM_SDK_NAMESPACE::TranscodingStatus status, const zchar_t* path) override {}
#endif
};

// Create the global instance
static ZoomRecordingListener g_recordingListener;
// ----------------------------------------------


// --- 1. THE ZOOM MEETING WALKIE-TALKIE (Must go first!) ---
class ZoomMeetingListener : public ZOOM_SDK_NAMESPACE::IMeetingServiceEvent {
public:
    virtual void onMeetingStatusChanged(ZOOM_SDK_NAMESPACE::MeetingStatus status, int iResult = 0) override {
        blog(LOG_INFO, "[Zoom to OBS] MEETING STATUS CHANGED: Status Code %d (Result: %d)", status, iResult);
        
        if (status == ZOOM_SDK_NAMESPACE::MEETING_STATUS_INMEETING) {
            blog(LOG_INFO, "[Zoom to OBS] SUCCESS! WE ARE OFFICIALLY IN THE MEETING!");
            
            // Hand the recording Walkie-Talkie to Zoom and wait for permission
            ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
            if (meeting_service) {
                ZOOM_SDK_NAMESPACE::IMeetingRecordingController* rec_ctrl = meeting_service->GetMeetingRecordingController();
                if (rec_ctrl) {
                    rec_ctrl->SetEvent(&g_recordingListener);
                    blog(LOG_INFO, "[Zoom to OBS] Waiting for Host to click 'Allow to Record Local Files'...");
                }
            }
            
        } else if (status == ZOOM_SDK_NAMESPACE::MEETING_STATUS_WAITINGFORHOST) {
            blog(LOG_INFO, "[Zoom to OBS] We are in the Waiting Room...");
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

// Create the global instance BEFORE the Auth Listener tries to use it
static ZoomMeetingListener g_meetingListener;
// ----------------------------------------------------------

// --- 2. THE ZOOM WALKIE-TALKIE (AUTH LISTENER) ---
class ZoomAuthListener : public ZOOM_SDK_NAMESPACE::IAuthServiceEvent {
public:
    virtual void onAuthenticationReturn(ZOOM_SDK_NAMESPACE::AuthResult ret) override {
        if (ret == ZOOM_SDK_NAMESPACE::AUTHRET_SUCCESS) {
            blog(LOG_INFO, "[Zoom to OBS] RADIO MESSAGE: SUCCESS! Zoom Engine is fully logged in and ready!");
            
            ZOOM_SDK_NAMESPACE::IMeetingService* meeting_service = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&meeting_service);
            
            if (meeting_service) {
                // Hand the meeting walkie-talkie to the engine
                meeting_service->SetEvent(&g_meetingListener);
                
                // --- PHASE 2: JOIN THE MEETING AS A GUEST ---
                ZOOM_SDK_NAMESPACE::JoinParam joinParam;
                joinParam.userType = ZOOM_SDK_NAMESPACE::SDK_UT_WITHOUT_LOGIN;
                
                ZOOM_SDK_NAMESPACE::JoinParam4WithoutLogin& param = joinParam.param.withoutloginuserJoin;
                param.meetingNumber = 7723013754ULL; // Your Meeting ID
                param.userName = L"OBS Camera Bot";
                param.psw = L""; 
                
                // Keep the bot's mic and camera off by default
                param.isAudioOff = true;
                param.isVideoOff = true;

                // Fire the JOIN command
                ZOOM_SDK_NAMESPACE::SDKError err = meeting_service->Join(joinParam);
                if (err == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
                    blog(LOG_INFO, "[Zoom to OBS] Join command successfully sent to engine!");
                } else {
                    blog(LOG_ERROR, "[Zoom to OBS] ERROR: Failed to send join command. Code: %d", err);
                }
            }
        } else {
            blog(LOG_ERROR, "[Zoom to OBS] RADIO MESSAGE: ERROR! Zoom rejected our JWT. Error Code: %d", ret);
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

// Create one global instance of our listener
static ZoomAuthListener g_authListener;
// ----------------------------------------------

// ----------------------------------------------------------------------------
// THE INDEPENDENT ZOOM SOURCE CLASS
// ----------------------------------------------------------------------------
class ZoomSource {
public:
    obs_source_t* source;
    ix::WebSocket webSocket;
    
    // Independent FFmpeg Brain
    const AVCodec* codec;
    AVCodecContext* codec_ctx;
    AVCodecParserContext* parser;
    AVPacket* pkt;
    AVFrame* frame;
    
    // Independent Clock
    uint64_t next_timestamp;

    ZoomSource(obs_source_t* src, const std::string& source_type_name) {
        source = src;
        next_timestamp = 0;
        
        // 1. Boot up this specific source's FFmpeg decoder
        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        parser = av_parser_init(codec->id);
        codec_ctx = avcodec_alloc_context3(codec);
        avcodec_open2(codec_ctx, codec, NULL);
        pkt = av_packet_alloc();
        frame = av_frame_alloc();

        // 2. Connect this specific source's WebSocket
        webSocket.setUrl("ws://localhost:8765");
        webSocket.setOnMessageCallback([this, source_type_name](const ix::WebSocketMessagePtr& msg) {
            
            if (msg->type == ix::WebSocketMessageType::Open) {
                blog(LOG_INFO, "[Zoom RTMS] %s CONNECTED!", source_type_name.c_str());
            }
            else if (msg->type == ix::WebSocketMessageType::Message) {
                uint8_t* data = (uint8_t*)msg->str.data();
                size_t data_size = msg->str.size();
                
                while (data_size > 0) {
                    int ret = av_parser_parse2(parser, codec_ctx, &pkt->data, &pkt->size,
                                               data, static_cast<int>(data_size), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
                    if (ret < 0) break;
                    
                    data += ret;
                    data_size -= ret;
                    
                    if (pkt->size) {
                        if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                            while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                                
                                // REVERTED TO PROVEN V1 API
                                struct obs_source_frame obs_frame = {};
                                obs_frame.format = VIDEO_FORMAT_I420; 
                                obs_frame.width = frame->width;
                                obs_frame.height = frame->height;
                                obs_frame.data[0] = frame->data[0];
                                obs_frame.data[1] = frame->data[1];
                                obs_frame.data[2] = frame->data[2];
                                obs_frame.linesize[0] = frame->linesize[0];
                                obs_frame.linesize[1] = frame->linesize[1];
                                obs_frame.linesize[2] = frame->linesize[2];
                                
                                // PROVEN COLOR MATRIX FIX
                                video_format_get_parameters(VIDEO_CS_DEFAULT, VIDEO_RANGE_PARTIAL, 
                                                            obs_frame.color_matrix, 
                                                            obs_frame.color_range_min, 
                                                            obs_frame.color_range_max);
                                
                                uint64_t now = os_gettime_ns();
                                if (next_timestamp == 0 || next_timestamp < now - 100000000ULL) {
                                    next_timestamp = now; 
                                }
                                obs_frame.timestamp = next_timestamp;
                                next_timestamp += 33333333ULL; 
                                
                                // OUTPUT VIA V1 API
                                obs_source_output_video(source, &obs_frame);
                            }
                        }
                    }
                }
            }
        });
        webSocket.start();
    }

    ~ZoomSource() {
