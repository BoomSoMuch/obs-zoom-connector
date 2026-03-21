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
    virtual void onAuthenticationReturn(ZOOM_SDK_NAMESPACE::AuthResult ret) override {
        if (ret == ZOOM_SDK_NAMESPACE::AUTHRET_SUCCESS) {
            blog(LOG_INFO, "[Zoom] SDK Auth Success. Launching SSO Login...");
            
            ZOOM_SDK_NAMESPACE::IAuthService* auth = nullptr;
            ZOOM_SDK_NAMESPACE::CreateAuthService(&auth);
            if (auth) {
                // Change "your-company" to your Zoom vanity URL prefix if you have one, 
                // otherwise "zoom" often works for standard accounts.
                const zchar_t* ssoUrl = auth->GenerateSSOLoginWebURL(L"zoom");
                if (ssoUrl) {
                    blog(LOG_INFO, "[Zoom] Opening browser for login...");
                    ShellExecuteW(NULL, L"open", ssoUrl, NULL, NULL, SW_SHOWNORMAL);
                }
            }
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
