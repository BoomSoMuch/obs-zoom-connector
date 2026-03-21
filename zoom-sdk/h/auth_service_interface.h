#include <obs-module.h>
#include <util/platform.h>
#include <windows.h>
#include <string>

#include "zoom_sdk.h"
#include "auth_service_interface.h"
#include "meeting_service_interface.h"
#include "meeting_service_components/meeting_recording_interface.h"
#include "meeting_service_components/meeting_participants_ctrl_interface.h"

static obs_source_t* g_participantSource = nullptr;

// --- AUTH LISTENER ---
class ZoomAuthListener : public ZOOM_SDK_NAMESPACE::IAuthServiceEvent {
public:
// 1. Inside your ZoomAuthListener
virtual void onLoginReturnWithReason(ZOOM_SDK_NAMESPACE::LOGINSTATUS ret, ZOOM_SDK_NAMESPACE::IAccountInfo* pAccountInfo, ZOOM_SDK_NAMESPACE::LoginFailReason reason) override {
    if (ret == ZOOM_SDK_NAMESPACE::LOGIN_SUCCESS) {
        blog(LOG_INFO, "[Zoom] Successfully logged in as Host: %ls", pAccountInfo->GetDisplayName());

        // 2. Now that we are LOGGED IN, we join the meeting. 
        // Because the SDK is authenticated as YOU, it will bypass the waiting room.
        ZOOM_SDK_NAMESPACE::IMeetingService* ms = nullptr;
        ZOOM_SDK_NAMESPACE::CreateMeetingService(&ms);
        if (ms) {
            ms->SetEvent(&g_meetingListener);
            
            // Note: We use JoinParam, but because we are logged in, 
            // we don't need to provide a ZAK token.
            ZOOM_SDK_NAMESPACE::JoinParam jp;
            jp.userType = ZOOM_SDK_NAMESPACE::SDK_UT_NORMALUSER; 
            auto& p = jp.param.normaluserJoin;
            p.meetingNumber = 7723013754ULL;
            p.userName = L"OBS Host Bot";
            
            SDKError err = ms->Join(jp);
            if (err != SDKERR_SUCCESS) {
                blog(LOG_ERROR, "[Zoom] Join failed with error code: %d", err);
            }
        }
    } else if (ret == ZOOM_SDK_NAMESPACE::LOGIN_FAILED) {
        blog(LOG_ERROR, "[Zoom] Login failed. Reason code: %d", reason);
    }
}

    virtual void onLoginReturnWithReason(ZOOM_SDK_NAMESPACE::LOGINSTATUS ret, ZOOM_SDK_NAMESPACE::IAccountInfo* p, ZOOM_SDK_NAMESPACE::LoginFailReason r) override {
        if (ret == ZOOM_SDK_NAMESPACE::LOGIN_SUCCESS) {
            blog(LOG_INFO, "[Zoom] Successfully logged in as: %ls", p->GetDisplayName());
            // Now you can Join() and you will be the HOST.
        }
    }

    // Required Stubs to prevent "Abstract Class" errors
    virtual void onLogout() override {}
    virtual void onZoomIdentityExpired() override {}
    virtual void onZoomAuthIdentityExpired() override {}
    virtual void onNotificationServiceStatus(ZOOM_SDK_NAMESPACE::SDKNotificationServiceStatus s, ZOOM_SDK_NAMESPACE::SDKNotificationServiceError e) override {}
};

static ZoomAuthListener g_authListener;

// --- OBS MODULE LOAD ---
bool obs_module_load(void) {
    // ... (Source registration code remains the same)

    ZOOM_SDK_NAMESPACE::InitParam ip;
    ip.strWebDomain = L"https://zoom.us";
    if (ZOOM_SDK_NAMESPACE::InitSDK(ip) == ZOOM_SDK_NAMESPACE::SDKERR_SUCCESS) {
        ZOOM_SDK_NAMESPACE::IAuthService* auth = nullptr;
        ZOOM_SDK_NAMESPACE::CreateAuthService(&auth);
        if (auth) {
            auth->SetEvent(&g_authListener);
            ZOOM_SDK_NAMESPACE::AuthContext ctx;
            ctx.jwt_token = L"PASTE_YOUR_JWT_HERE";
            auth->SDKAuth(ctx);
        }
    }
    return true;
}

void obs_module_unload(void) {}
OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-zoom-connector", "en-US")
