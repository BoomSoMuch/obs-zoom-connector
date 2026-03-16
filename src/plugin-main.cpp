#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <obs-module.h>
#include <util/platform.h>
#include <graphics/graphics.h>
#include <ixwebsocket/IXWebSocket.h>
#include <string>

// ----------------------------------------------------------------------------
// 1 & 2. PARTICIPANT & SCREENSHARE (Keeping these as colored boxes for now)
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
// 3. ZOOM GALLERY SOURCE (The real video feed!)
// ----------------------------------------------------------------------------
// We need a global pointer so the WebSocket can push frames directly to this source
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
// WEBSOCKET RECEIVER & DECODER
// ----------------------------------------------------------------------------
static ix::WebSocket g_webSocket;

void setup_websocket() {
    g_webSocket.setUrl("ws://localhost:8765");
    
    g_webSocket.setOnMessageCallback([](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Open) {
            blog(LOG_INFO, "[Zoom RTMS] CONNECTED TO FAKE ZOOM SERVER!");
        }
        else if (msg->type == ix::WebSocketMessageType::Message) {
            // If the user hasn't added the Zoom Gallery source to their scene yet, do nothing.
            if (!g_zoom_gallery_source) return; 

            // 1. Decode the incoming JPEG bytes into raw RGBA pixels
            int width, height, channels;
            unsigned char* pixels = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc*>(msg->str.data()),
                static_cast<int>(msg->str.size()),
                &width, &height, &channels, 4); // Force 4 channels (RGBA)

            if (pixels) {
                // 2. Wrap the pixels in an OBS video frame
                struct obs_source_frame frame = {};
                frame.format = VIDEO_FORMAT_RGBA;
                frame.width = width;
                frame.height = height;
                frame.linesize[0] = width * 4;
                frame.data[0] = pixels;
                frame.timestamp = os_gettime_ns(); // Give it a timestamp so OBS knows when to play it

                // 3. Push the frame directly into the OBS video pipeline!
                obs_source_output_video(g_zoom_gallery_source, &frame);

                // 4. Clean up the memory so we don't crash
                stbi_image_free(pixels);
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
    // Setup Participant Info
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_participant_info.get_name = [](void*) { return "Zoom Participant"; };
    zoom_participant_info.create = dummy_create;
    zoom_participant_info.destroy = dummy_destroy;
    zoom_participant_info.video_render = zp_video_render;
    zoom_participant_info.get_width = dummy_get_width;
    zoom_participant_info.get_height = dummy_get_height;

    // Setup Screenshare Info
    zoom_screenshare_info.id = "zoom_screenshare_source";
    zoom_screenshare_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_screenshare_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_screenshare_info.get_name = [](void*) { return "Zoom Screenshare"; };
    zoom_screenshare_info.create = dummy_create;
    zoom_screenshare_info.destroy = dummy_destroy;
    zoom_screenshare_info.video_render = zs_video_render;
    zoom_screenshare_info.get_width = dummy_get_width;
    zoom_screenshare_info.get_height = dummy_get_height;

    // Setup Gallery Info (NOTICE: It is now ASYNC_VIDEO, which means it receives live frames!)
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
}
