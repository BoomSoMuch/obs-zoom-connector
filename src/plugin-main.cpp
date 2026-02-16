#include <obs-module.h>
#include <graphics/graphics.h>
#include <string>

// ----------------------------------------------------------------------------
// 1. ZOOM PARTICIPANT SOURCE
// ----------------------------------------------------------------------------

struct ZoomParticipantData {
    std::string name;
};

const char* zp_get_name(void* type_data) {
    return "Zoom Participant";
}

void* zp_create(obs_data_t* settings, obs_source_t* source) {
    return new ZoomParticipantData();
}

void zp_destroy(void* data) {
    ZoomParticipantData* p = static_cast<ZoomParticipantData*>(data);
    delete p;
}

// NEW: Required for video sources
uint32_t zp_get_width(void* data) {
    return 1920;
}

// NEW: Required for video sources
uint32_t zp_get_height(void* data) {
    return 1080;
}

void zp_video_render(void* data, gs_effect_t* effect) {
    struct vec4 color;
    vec4_set(&color, 0.17f, 0.5f, 1.0f, 1.0f); // Zoom Blue
    
    gs_clear(GS_CLEAR_COLOR, &color, 0.0f, 0);
}

struct obs_source_info zoom_participant_info = {};

void init_zoom_participant() {
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_participant_info.get_name = zp_get_name;
    zoom_participant_info.create = zp_create;
    zoom_participant_info.destroy = zp_destroy;
    zoom_participant_info.video_render = zp_video_render;
    zoom_participant_info.get_width = zp_get_width;   // <--- Added
    zoom_participant_info.get_height = zp_get_height; // <--- Added
}


// ----------------------------------------------------------------------------
// 2. ZOOM SCREENSHARE SOURCE
// ----------------------------------------------------------------------------

struct ZoomScreenData {
    std::string name;
};

const char* zs_get_name(void* type_data) {
    return "Zoom Screenshare";
}

void* zs_create(obs_data_t* settings, obs_source_t* source) {
    return new ZoomScreenData();
}

void zs_destroy(void* data) {
    ZoomScreenData* p = static_cast<ZoomScreenData*>(data);
    delete p;
}

// NEW: Required for video sources
uint32_t zs_get_width(void* data) {
    return 1920;
}

// NEW: Required for video sources
uint32_t zs_get_height(void* data) {
    return 1080;
}

void zs_video_render(void* data, gs_effect_t* effect) {
    struct vec4 color;
    vec4_set(&color, 0.0f, 0.8f, 0.2f, 1.0f); // Green
    
    gs_clear(GS_CLEAR_COLOR, &color, 0.0f, 0);
}

struct obs_source_info zoom_screenshare_info = {};

void init_zoom_screenshare() {
    zoom_screenshare_info.id = "zoom_screenshare_source";
    zoom_screenshare_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_screenshare_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_screenshare_info.get_name = zs_get_name;
    zoom_screenshare_info.create = zs_create;
    zoom_screenshare_info.destroy = zs_destroy;
    zoom_screenshare_info.video_render = zs_video_render;
    zoom_screenshare_info.get_width = zs_get_width;   // <--- Added
    zoom_screenshare_info.get_height = zs_get_height; // <--- Added
}


// ----------------------------------------------------------------------------
// PLUGIN REGISTRATION
// ----------------------------------------------------------------------------

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    init_zoom_participant();
    init_zoom_screenshare();

    obs_register_source(&zoom_participant_info);
    obs_register_source(&zoom_screenshare_info);

    return true;
}
