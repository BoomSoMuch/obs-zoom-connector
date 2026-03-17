extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}
#include <obs-module.h>
#include <util/platform.h>
#include <ixwebsocket/IXWebSocket.h>
#include <string>

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
        webSocket.stop();
        if (parser) av_parser_close(parser);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
    }
};

// ----------------------------------------------------------------------------
// OBS C-API BRIDGES
// ----------------------------------------------------------------------------
void* zg_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Gallery"); }
void* zp_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Participant"); }
void* zs_create(obs_data_t* settings, obs_source_t* source) { return new ZoomSource(source, "Screenshare"); }

void z_destroy(void* data) { 
    delete static_cast<ZoomSource*>(data); 
}

// ----------------------------------------------------------------------------
// PLUGIN REGISTRATION
// ----------------------------------------------------------------------------
struct obs_source_info zoom_participant_info = {};
struct obs_source_info zoom_screenshare_info = {};
struct obs_source_info zoom_gallery_info = {};

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

    zoom_gallery_info.id = "zoom_gallery_source";
    zoom_gallery_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_gallery_info.output_flags = OBS_SOURCE_ASYNC_VIDEO; 
    zoom_gallery_info.get_name = [](void*) { return "Zoom Gallery"; };
    zoom_gallery_info.create = zg_create;
    zoom_gallery_info.destroy = z_destroy;

    obs_register_source(&zoom_participant_info);
    obs_register_source(&zoom_screenshare_info);
    obs_register_source(&zoom_gallery_info);

    return true;
}

void obs_module_unload(void) {}
