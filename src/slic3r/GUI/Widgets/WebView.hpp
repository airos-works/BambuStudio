#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <optional>

#include <wx/webview.h>

#include "WebViewWatcher.hpp"

class WebView
{
public:
    // Creation.
    static wxWebView *CreateWebView(
        wxWindow *parent, wxString const &url, wxString const &name = wxEmptyString,
        Slic3r::GUI::WebViewProtectionMode mode = Slic3r::GUI::WebViewProtectionMode::DiagnosticsOnly);
    static std::optional<Slic3r::GUI::WebViewWatcher::Fault> CreationFault(wxWebView *webView);
    static bool ValidateLocalTarget(wxWebView *webView, const wxString &url);

    // Page operations.
    static void LoadUrl(wxWebView * webView, wxString const &url);
    static bool RunScript(wxWebView *webView, wxString const &msg, bool force_execute = false);

    // Ready watchdog.
    static void StartReadyWatchdog(wxWebView *webView);
    static void CancelReadyWatchdog(wxWebView *webView);
    static bool NotifyReady(wxWebView *webView);
    static void SetReadyWatchdogPaused(wxWebView *webView, bool paused);

    // Recovery.
    static void CancelRecovery(wxWebView *webView);
    static void RecreateAll();

    // Cookies.
    // Remove WebView cookies named "token" on domains containing "bambulab".
    // Windows: WebView2 CookieManager; macOS: WKHTTPCookieStore (default data store).
    static void ClearBambulabTokenCookies();

    // Engine release. Whether a web view may give back its engine (web process)
    // when it is hidden, blanked or destroyed. Always true, except on Haiku with
    // a GL renderer add-on that aborts the process when an engine view goes
    // away (Zink before mesa-hgl 2e046a9, ZINK-TEARDOWN): decided by
    // OnOpenGLReady(), false until then.
    static bool CanReleaseEngine();
    static void OnOpenGLReady(const std::string &gl_renderer);

    // Backend setup.
    static wxString BuildEdgeUserDataPath();
};

#endif // !slic3r_GUI_WebView_hpp_
