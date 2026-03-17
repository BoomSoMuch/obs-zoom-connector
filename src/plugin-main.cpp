extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}
#include <obs-module.h>
#include <util/platform.h>
#include <ixwebsocket/IXWebSocket.h>
#include <string>

// ----------------------------------------------------------------------------
// DUMMY SOURCES (Participant & Screenshare)
// ----------------------------------------------------------------------------
struct DummyData { std::string name; };
void* dummy_create(obs_data_t*, obs_source_t*) { return new DummyData(); }
void dummy_destroy(void* data) { delete static_cast<DummyData*>(data); }
uint32_t dummy_get_width(void*) { return 1920; }
uint32_t dummy_get_height(void*) { return 1080; }

void zp_video_render(void*, gs_effect_t*) {
    struct vec4 color; vec4_set(&color, 0.17f, 0.5f, 1.0f, 1.0f); gs_clear(GS_CLEAR_COLOR, &color, 0.0f, 0);
}
struct obs_source_info zoom_participant_info = {};

void zs_video_render(void*, gs_effect_t*) {
    struct vec4 color; vec4_set(&color, 0.0f, 0.8f, 0.2f, 1.0f); gs_clear(GS_CLEAR_COLOR, &color, 0.0f, 0);
}
struct obs_source_info zoom_screenshare_info = {};

// ----------------------------------------------------------------------------
// ZOOM GALLERY SOURCE (ASYNC VIDEO)
// ----------------------------------------------------------------------------
obs_source_t* g_zoom_gallery_source = nullptr;

void* zg_create(obs_data_t* settings, obs_source_t* source) {
    g_zoom_gallery_source = source;
    return new DummyData();
}
void zg_destroy(void* data) {
    g_zoom_gallery_source = nullptr;
    delete static_cast<DummyData*>(data);
}
struct obs_source_info zoom_gallery_info = {};

// ----------------------------------------------------------------------------
// FFMPEG DECODER STATE
// ----------------------------------------------------------------------------
const AVCodec* g_codec = nullptr;
AVCodecContext* g_codec_ctx = nullptr;
AVCodecParserContext* g_parser = nullptr;
AVPacket* g_pkt = nullptr;
AVFrame* g_frame = nullptr;

void init_ffmpeg() {
    g_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    g_parser = av_parser_init(g_codec->id);
    g_codec_ctx = avcodec_alloc_context3(g_codec);
    avcodec_open2(g_codec_ctx, g_codec, NULL);
    g_pkt = av_packet_alloc();
    g_frame = av_frame_alloc();
}

void cleanup_ffmpeg() {
    if (g_parser) av_parser_close(g_parser);
    if (g_codec_ctx) avcodec_free_context(&g_codec_ctx);
    if (g_frame) av_frame_free(&g_frame);
    if (g_pkt) av_packet_free(&g_pkt);
}

// ----------------------------------------------------------------------------
// WEBSOCKET RECEIVER
// ----------------------------------------------------------------------------
static ix::WebSocket g_webSocket;

void setup_websocket() {
    init_ffmpeg(); 
    
    g_webSocket.setUrl("ws://localhost:8765");
    g_webSocket.setOnMessageCallback([](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            blog(LOG_INFO, "[Zoom RTMS] CONNECTED TO FAKE ZOOM SERVER!");
        }
        else if (msg->type == ix::WebSocketMessageType::Message) {
            if (!g_zoom_gallery_source) return;

            uint8_t* data = (uint8_t*)msg->str.data();
            size_t data_size = msg->str.size();
            
            while (data_size > 0) {
                int ret = av_parser_parse2(g_parser, g_codec_ctx, &g_pkt->data, &g_pkt->size,
                                           data, static_cast<int>(data_size), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
                if (ret < 0) break;
                
                data += ret;
                data_size -= ret;
                
                if (g_pkt->size) {
                    if (avcodec_send_packet(g_codec_ctx, g_pkt) == 0) {
                        
                        while (avcodec_receive_frame(g_codec_ctx, g_frame) == 0) {
                            
                            struct obs_source_frame obs_frame = {};
                            obs_frame.format = VIDEO_FORMAT_I420; 
                            obs_frame.width = g_frame->width;
                            obs_frame.height = g_frame->height;
                            obs_frame.data[0] = g_frame->data[0];
                            obs_frame.data[1] = g_frame->data[1];
                            obs_frame.data[2] = g_frame->data[2];
                            obs_frame.linesize[0] = g_frame->linesize[0];
                            obs_frame.linesize[1] = g_frame->linesize[1];
                            obs_frame.linesize[2] = g_frame->linesize[2];
                            
                            // --- THE PACEMAKER ---
                            static uint64_t next_timestamp = 0;
                            uint64_t now = os_gettime_ns();
                            
                            // Sync the clock to right now if we fell behind
                            if (next_timestamp == 0 || next_timestamp < now) {
                                next_timestamp = now; 
                            }
                            
                            obs_frame.timestamp = next_timestamp;
                            
                            // Advance clock by 33.3ms for perfect 30fps
                            next_timestamp += 33333333ULL;
                            // ---------------------
                            
                            obs_source_output_video(g_zoom_gallery_source, &obs_frame);
                        }
                    }
                }
            }
        }
    });
    g_webSocket.start();
}
// ----------------------------------------------------------------------------
// PLUGIN REGISTRATION
// ----------------------------------------------------------------------------
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_participant_info.get_name = [](void*) { return "Zoom Participant"; };
    zoom_participant_info.create = dummy_create;
    zoom_participant_info.destroy = dummy_destroy;
    zoom_participant_info.video_render = zp_video_render;
    zoom_participant_info.get_width = dummy_get_width;
    zoom_participant_info.get_height = dummy_get_height;

    zoom_screenshare_info.id = "zoom_screenshare_source";
    zoom_screenshare_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_screenshare_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_screenshare_info.get_name = [](void*) { return "Zoom Screenshare"; };
    zoom_screenshare_info.create = dummy_create;
    zoom_screenshare_info.destroy = dummy_destroy;
    zoom_screenshare_info.video_render = zs_video_render;
    zoom_screenshare_info.get_width = dummy_get_width;
    zoom_screenshare_info.get_height = dummy_get_height;

    zoom_gallery_info.id = "zoom_gallery_source";
    zoom_gallery_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_gallery_info.output_flags = OBS_SOURCE_ASYNC_VIDEO; 
    zoom_gallery_info.get_name = [](void*) { return "Zoom Gallery"; };
    zoom_gallery_info.create = zg_create;
    zoom_gallery_info.destroy = zg_destroy;

    obs_register_source(&zoom_participant_info);
    obs_register_source(&zoom_screenshare_info);
    obs_register_source(&zoom_gallery_info);

    setup_websocket();
    return true;
}

void obs_module_unload(void) {
    g_webSocket.stop();
    cleanup_ffmpeg();
}
