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
#include "meeting_service_components/meeting_ui_ctrl_interface.h"
#include "rawdata/rawdata_renderer_interface.h"
#include "rawdata/zoom_rawdata_api.h"

static obs_source_t* g_participantSource = nullptr;

// --- VIDEO RENDERER ---
class ZoomVideoCatcher : public ZOOM_SDK_NAMESPACE::IZoomSDKRendererDelegate {
public:
    virtual void onRawDataFrameReceived(YUVRawDataI420* data) override {
        if (!g_participantSource) return;
        struct obs_source_frame obs_frame = {};
        obs_frame.format = VIDEO_FORMAT_I420; 
        obs_frame.width = data->GetStreamWidth();
        obs_frame.height = data->GetStreamHeight();
        obs_frame.data[0] = (uint8_t*)data->GetYBuffer();
        obs_frame.data[1] = (uint8_t*)data->GetUBuffer();
        obs_frame.data[2] = (uint8_t*)data->GetVBuffer();
        obs_frame.linesize[0] = obs_frame.width;
        obs_frame.linesize[1] = obs_frame.width / 2;
        obs_frame.linesize[2] = obs_frame.width / 2;
        obs_frame.timestamp = os_gettime_ns();
        obs_source_output_video(g_participantSource, &obs_frame);
    }
    virtual void onRawDataStatusChanged(RawDataStatus status) override {}
    virtual void onRendererBeDestroyed() override {}
};

static ZoomVideoCatcher g_videoCatcher;
static ZOOM_SDK_NAMESPACE::IZoomSDKRenderer* g_videoRenderer = nullptr;

// --- RECORDING CONTROLLER (STUBBED FOR YOUR SDK) ---
class ZoomRecordingListener : public ZOOM_SDK_NAMESPACE::IMeetingRecordingCtrlEvent {
public:
    virtual void onRecordPrivilegeChanged(bool bCanRec) override {
        if (bCanRec) {
            ZOOM_SDK_NAMESPACE::IMeetingService* ms = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&ms);
            if (ms && ms->GetMeetingRecordingController()) {
                ms->GetMeetingRecordingController()->StartRawRecording();
                ZOOM_SDK_NAMESPACE::createRenderer(&g_videoRenderer, &g_videoCatcher);
                if (g_videoRenderer) {
                    auto part_ctrl = ms->GetMeetingParticipantsController();
                    if (part_ctrl && part_ctrl->GetParticipantsList()) {
                        unsigned int first_user = part_ctrl->GetParticipantsList()->GetItem(0);
                        g_videoRenderer->subscribe(first_user, ZOOM_SDK_NAMESPACE::RAW_DATA_TYPE_VIDEO);
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
    virtual void onTranscodingStatusChanged(ZOOM_SDK_NAMESPACE::TranscodingStatus status) override {} 
};
static ZoomRecordingListener g_recordingListener;

// --- MEETING SERVICE (STUBBED FOR YOUR SDK) ---
class ZoomMeetingListener : public ZOOM_SDK_NAMESPACE::IMeetingServiceEvent {
public:
    virtual void onMeetingStatusChanged(ZOOM_SDK_NAMESPACE::MeetingStatus status, int iResult = 0) override {
        if (status == ZOOM_SDK_NAMESPACE::MEETING_STATUS_INMEETING) {
            ZOOM_SDK_NAMESPACE::IMeetingService* ms = nullptr;
            ZOOM_SDK_NAMESPACE::CreateMeetingService(&ms);
            if (ms && ms->GetMeetingRecordingController()) {
                ms->GetMeetingRecordingController()->SetEvent(&g_recordingListener);
                g_recordingListener.onRecordPrivilegeChanged(true);
            }
        }
    }
    virtual void onMeetingStatisticsWarningNotification(ZOOM_SDK_NAMESPACE::StatisticsWarningType t) override {}
    virtual void onMeetingParameterNotification(const ZOOM_SDK_NAMESPACE
