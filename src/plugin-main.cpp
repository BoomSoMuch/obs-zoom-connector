#include <obs-module.h>
#include <string>

// ----------------------------------------------------------------------------
// 1. ZOOM PARTICIPANT SOURCE
// ----------------------------------------------------------------------------

struct ZoomParticipantData {
    std::string name;
};

// Returns the display name of the source
const char* zp_get_name(void* type_data) {
    return "Zoom Participant";
}

// Called when the user adds the source
void* zp_create(obs_data_t* settings, obs_source_t* source) {
    ZoomParticipantData* data = new ZoomParticipantData();
    return data;
}

// Called when the source is removed
void zp_destroy(void* data) {
    ZoomParticipantData* p = static_cast<ZoomParticipantData*>(data);
    delete p;
}

// A dummy render function (draws a placeholder box so you can see it)
void zp_video_render(void* data, gs_effect_t* effect) {
    // This just clears the source area to a dark Zoom-blue color for the demo
    gs_clear(GS_CLEAR_COLOR, (struct vec4*) & (struct vec4){0.17f, 0.5f, 1.0f, 1.0f}, 0.0f, 0);
}

// Defines the source properties
struct obs_source_info zoom_participant_info = {};

void init_zoom_participant() {
    zoom_participant_info.id = "zoom_participant_source";
    zoom_participant_info.type = OBS_SOURCE_TYPE_INPUT;
    zoom_participant_info.output_flags = OBS_SOURCE_VIDEO;
    zoom_participant_info.get_name = zp_get_name;
    zoom_participant_info.create = zp_create;
    zoom_participant_info.destroy = zp_destroy;
    zoom_participant_info.video_render = zp_video_render;
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
    ZoomScreenData* data = new ZoomScreenData();
    return data;
}

void zs_destroy(void* data) {
    ZoomScreenData* p = static_cast<ZoomScreenData*>(data);
    delete p;
}

void zs_video_render(void* data, gs_effect_t* effect) {
    // Clears to a green color to distinguish it from participants
    gs_clear(GS_CLEAR_COLOR, (struct vec4*) & (struct vec4){0.0f, 0.8f, 0.2f, 1.0f}, 0.0f, 0);
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
}


// ----------------------------------------------------------------------------
// PLUGIN REGISTRATION
// ----------------------------------------------------------------------------

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")

bool obs_module_load(void) {
    // Initialize our structure data
    init_zoom_participant();
    init_zoom_screenshare();

    // Register the sources with OBS
    obs_register_source(&zoom_participant_info);
    obs_register_source(&zoom_screenshare_info);

    return true;
}
