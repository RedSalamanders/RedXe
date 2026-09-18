// ZoomTests: the Zoom service without Zoom, a network, or the SDK. Covers the settings model, the OAuth PKCE
// material and token parsing, the loopback redirect listener driven by a local client socket, the in-memory
// credential store, and the shipped zoom.action.dll driven through the synthetic session: contracts, sign-in,
// connection, every action, role refusals, reconnect, and stop.

#include "Actions/ActionTargets.h"
#include "PlugInterfaces/Action.h"
#include "PlugInterfaces/Factory.h"
#include "PlugInterfaces/Host.h"
#include "PlugInterfaces/Service.h"
#include "PlugInterfaces/Widget.h"
#include "ZoomAuth.h"
#include "ZoomLocal.h"
#include "ZoomSession.h"
#include "ZoomSettings.h"
#include "ZoomTestContract.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <windows.h>

#include <shellapi.h>

#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027 28182)
#include <wil/com.h>
#include <wil/resource.h>
#pragma warning(pop)

namespace
{
constexpr HRESULT kTestFailure = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

#define ZOOM_CHECK(condition, message)                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::wprintf(L"FAIL %S (%S:%d)\n", message, __FILE__, __LINE__);                                           \
            return kTestFailure;                                                                                       \
        }                                                                                                              \
    } while (false)

template <typename Function> [[nodiscard]] Function Resolve(HMODULE module, const char* name) noexcept
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

[[nodiscard]] HRESULT BuildSiblingPath(const wchar_t* relativePath, std::array<wchar_t, 1024>& path) noexcept
{
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return E_FAIL;
    }
    wchar_t* separator = wcsrchr(path.data(), L'\\');
    if (!separator)
    {
        return E_FAIL;
    }
    separator[1] = L'\0';
    return wcscat_s(path.data(), path.size(), relativePath) == 0 ? S_OK : E_FAIL;
}

template <typename Predicate> [[nodiscard]] bool WaitUntil(Predicate predicate, DWORD milliseconds) noexcept
{
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    while (GetTickCount64() < deadline)
    {
        if (predicate())
        {
            return true;
        }
        Sleep(10);
    }
    return predicate();
}

// A stand-in for the Zoom meeting window: a top-level window with push buttons whose captions are the toolbar
// names the local path reads and presses through MSAA (a Win32 button's default action is a click, delivered as
// BN_CLICKED to this window). It lives on its own pumping thread, like Zoom's window lives in its own process, so an
// accessibility client on another thread can query it.
class MeetingWindow final
{
  public:
    static constexpr size_t kButtons = 4;

    explicit MeetingWindow(const std::array<const wchar_t*, kButtons>& captions) noexcept
    {
        std::array<const wchar_t*, kButtons> copy = captions;
        _thread = std::thread(
            [this, copy]() noexcept
            {
                WNDCLASSW windowClass{};
                windowClass.lpfnWndProc = &MeetingWindow::WindowProc;
                windowClass.hInstance = GetModuleHandleW(nullptr);
                windowClass.lpszClassName = L"RedXe.ZoomTests.MeetingWindow";
                (void)RegisterClassW(&windowClass);
                _window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, windowClass.lpszClassName,
                                          L"Zoom Meeting (test)", WS_POPUP | WS_CLIPCHILDREN, -20000, -20000, 480, 80,
                                          nullptr, nullptr, windowClass.hInstance, this);
                for (size_t index = 0; _window && index < kButtons; ++index)
                {
                    _buttons[index] = CreateWindowExW(0, L"BUTTON", copy[index], WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                                      static_cast<int>(index) * 110, 10, 100, 30, _window,
                                                      reinterpret_cast<HMENU>(static_cast<uintptr_t>(100 + index)),
                                                      windowClass.hInstance, nullptr);
                }
                if (_window)
                {
                    (void)ShowWindow(_window, SW_SHOWNOACTIVATE);
                }
                _ready.store(true, std::memory_order_release);
                MSG message{};
                while (GetMessageW(&message, nullptr, 0, 0) > 0)
                {
                    (void)TranslateMessage(&message);
                    (void)DispatchMessageW(&message);
                }
                if (_window)
                {
                    (void)DestroyWindow(_window);
                    _window = nullptr;
                }
            });
        (void)WaitUntil([this]() noexcept { return _ready.load(std::memory_order_acquire); }, 5000);
    }
    ~MeetingWindow()
    {
        if (_thread.joinable())
        {
            (void)PostThreadMessageW(GetThreadId(_thread.native_handle()), WM_QUIT, 0, 0);
            _thread.join();
        }
    }
    MeetingWindow(const MeetingWindow&) = delete;
    MeetingWindow& operator=(const MeetingWindow&) = delete;

    [[nodiscard]] HWND Handle() const noexcept
    {
        return _window;
    }
    // Cross-thread SetWindowText is delivered through the window's own queue.
    void SetCaption(size_t index, const wchar_t* caption) noexcept
    {
        if (index < kButtons && _buttons[index])
        {
            (void)SetWindowTextW(_buttons[index], caption);
        }
    }
    [[nodiscard]] uint32_t Clicks(size_t index) const noexcept
    {
        return index < kButtons ? _clicks[index].load(std::memory_order_acquire) : 0;
    }

  private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept
    {
        if (message == WM_NCCREATE)
        {
            const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        else if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED)
        {
            MeetingWindow* self = reinterpret_cast<MeetingWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
            const size_t index = static_cast<size_t>(LOWORD(wParam)) - 100;
            if (self && index < kButtons)
            {
                self->_clicks[index].fetch_add(1, std::memory_order_acq_rel);
            }
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    std::thread _thread;
    std::atomic<bool> _ready{false};
    HWND _window = nullptr;
    std::array<HWND, kButtons> _buttons{};
    std::array<std::atomic<uint32_t>, kButtons> _clicks{};
};

// A host that records every action the service requests (zoom.signIn launches the browser through the host).
class TestHost : public IRedXeHost
{
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) noexcept override
    {
        if (!result)
        {
            return E_POINTER;
        }
        *result = nullptr;
        if (interfaceId == __uuidof(IUnknown) || interfaceId == __uuidof(IRedXeHost))
        {
            *result = static_cast<IRedXeHost*>(this);
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override
    {
        return 2;
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
        return 1;
    }
    HRESULT STDMETHODCALLTYPE GetDataProvider(const char*, IRedXeDataProvider** provider) noexcept override
    {
        if (provider)
        {
            *provider = nullptr;
        }
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE RequestFrame() noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ReportWidgetStatus(const char*, const RedXeWidgetStatusReport*) noexcept override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PersistWidgetSettings(const char*, const char*, uint32_t) noexcept override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord* record) noexcept override
    {
        if (!record || record->sizeBytes != sizeof(RedXeLogRecord) || !record->eventId)
        {
            return E_INVALIDARG;
        }
        {
            const auto guard = wil::AcquireSRWLockExclusive(&lock);
            strncpy_s(lastEvent, record->eventId, _TRUNCATE);
        }
        logs.fetch_add(1, std::memory_order_relaxed);
        if (std::strcmp(record->eventId, "zoom-state-unknown") == 0)
        {
            stateUnknownLogs.fetch_add(1, std::memory_order_relaxed);
        }
        if (std::strcmp(record->eventId, "zoom-local-mode") == 0)
        {
            localModeLogs.fetch_add(1, std::memory_order_relaxed);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueueControlWork(IRedXeControlWork*) noexcept override
    {
        return E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest* request) noexcept override
    {
        if (!request || request->sizeBytes != sizeof(RedXeActionRequest) || !request->actionUtf8)
        {
            return E_INVALIDARG;
        }
        {
            const auto guard = wil::AcquireSRWLockExclusive(&lock);
            strncpy_s(lastAction, request->actionUtf8, _TRUNCATE);
            strncpy_s(lastTarget, request->targetUtf8 ? request->targetUtf8 : "", _TRUNCATE);
        }
        actions.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ExecuteAction(const RedXeActionRequest* request) noexcept override
    {
        return RequestAction(request);
    }
    HRESULT STDMETHODCALLTYPE ValidateAction(const RedXeActionRequest*,
                                             const RedXeActionDescriptor** descriptor) noexcept override
    {
        if (descriptor)
        {
            *descriptor = nullptr;
        }
        return S_OK;
    }
    [[nodiscard]] bool SawEvent(const char* eventId) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&lock);
        return std::strcmp(lastEvent, eventId) == 0;
    }
    [[nodiscard]] std::string LastTarget() noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&lock);
        return lastTarget;
    }
    [[nodiscard]] bool LastActionIs(const char* name) noexcept
    {
        const auto guard = wil::AcquireSRWLockShared(&lock);
        return std::strcmp(lastAction, name) == 0;
    }

    std::atomic<uint32_t> logs{0};
    std::atomic<uint32_t> actions{0};
    std::atomic<uint32_t> stateUnknownLogs{0};
    std::atomic<uint32_t> localModeLogs{0};
    SRWLOCK lock = SRWLOCK_INIT;
    char lastEvent[65]{};
    char lastAction[65]{};
    char lastTarget[1025]{};
};

[[nodiscard]] HRESULT TestSettingsModel() noexcept
{
    using namespace Zoom;
    Settings settings{};
    std::array<char, 160> diagnostic{};
    ZOOM_CHECK(FAILED(ParseSettingsJson(kSettingsDefaults, settings, diagnostic.data(), diagnostic.size())),
               "the defaults alone are incomplete: clientId is required");
    ZOOM_CHECK(SUCCEEDED(ParseSettingsJson(R"json({"clientId":"abc123"})json", settings, diagnostic.data(),
                                           diagnostic.size())) &&
                   settings.ClientId() == "abc123" && settings.redirectPort == kDefaultRedirectPort &&
                   settings.Domain() == "zoom.us" && settings.DisplayName().empty() && !settings.autoConnect,
               "a client id with defaults parses");
    ZOOM_CHECK(
        SUCCEEDED(ParseSettingsJson(
            R"json({"clientId":"abc","redirectPort":50000,"domain":"zoom.example","displayName":"Desk","autoConnect":true})json",
            settings, diagnostic.data(), diagnostic.size())) &&
            settings.redirectPort == 50000 && settings.Domain() == "zoom.example" && settings.DisplayName() == "Desk" &&
            settings.autoConnect,
        "every member parses");
    ZOOM_CHECK(FAILED(ParseSettingsJson(R"json({"clientId":"abc","redirectPort":80})json", settings, diagnostic.data(),
                                        diagnostic.size())),
               "a privileged port is rejected");
    ZOOM_CHECK(FAILED(ParseSettingsJson(R"json({"clientId":"abc","clientSecret":"x"})json", settings, diagnostic.data(),
                                        diagnostic.size())),
               "a secret member is rejected");
    ZOOM_CHECK(FAILED(ParseSettingsJson(R"json({"clientId":""})json", settings, diagnostic.data(), diagnostic.size())),
               "an empty client id is rejected");
    // The local path members: mode and the toolbar labels.
    ZOOM_CHECK(SUCCEEDED(ParseSettingsJson(kSettingsDefaults, settings, diagnostic.data(), diagnostic.size())) == false,
               "defaults still need a client id in mode auto");
    ZOOM_CHECK(SUCCEEDED(ParseSettingsJson(
                   R"json({"clientId":"abc","mode":"sdk","labels":{"muted":"actuellement coupé"}})json", settings,
                   diagnostic.data(), diagnostic.size())) &&
                   settings.mode == Mode::Sdk && settings.LabelText(Label::Muted) == "actuellement coupé" &&
                   settings.LabelText(Label::Unmuted) == "currently unmuted" &&
                   settings.LabelText(Label::End) == "End,",
               "mode and one overridden label keep the other defaults");
    ZOOM_CHECK(
        SUCCEEDED(ParseSettingsJson(R"json({"mode":"local"})json", settings, diagnostic.data(), diagnostic.size())) &&
            settings.mode == Mode::Local && settings.clientIdBytes == 0,
        "mode local needs no client id");
    ZOOM_CHECK(FAILED(ParseSettingsJson(R"json({"clientId":"abc","mode":"keys"})json", settings, diagnostic.data(),
                                        diagnostic.size())) &&
                   FAILED(ParseSettingsJson(R"json({"clientId":"abc","labels":{"mute":"x"}})json", settings,
                                            diagnostic.data(), diagnostic.size())) &&
                   FAILED(ParseSettingsJson(R"json({"clientId":"abc","labels":{"muted":""}})json", settings,
                                            diagnostic.data(), diagnostic.size())) &&
                   FAILED(ParseSettingsJson(R"json({"clientId":"abc","globalShortcuts":true})json", settings,
                                            diagnostic.data(), diagnostic.size())),
               "an unknown mode, an unknown or empty label, and an unknown member are rejected");
    return S_OK;
}

[[nodiscard]] HRESULT TestAuthMaterial() noexcept
{
    using namespace Zoom;
    std::array<char, kVerifierCharacters + 1> verifier{};
    std::array<char, kVerifierCharacters + 1> other{};
    ZOOM_CHECK(GenerateVerifier(verifier.data(), verifier.size()) && GenerateVerifier(other.data(), other.size()) &&
                   std::strlen(verifier.data()) == kVerifierCharacters &&
                   std::strcmp(verifier.data(), other.data()) != 0,
               "verifiers are 64 base64url characters and random");
    for (const char character : std::string_view{verifier.data()})
    {
        const bool allowed = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                             (character >= '0' && character <= '9') || character == '-' || character == '_';
        ZOOM_CHECK(allowed, "verifier alphabet is base64url");
    }
    // RFC 7636 appendix B vector.
    std::array<char, kChallengeCharacters + 1> challenge{};
    ZOOM_CHECK(ComputeChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", challenge.data(), challenge.size()) &&
                   std::string_view{challenge.data()} == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
               "S256 challenge matches the RFC 7636 vector");
    std::array<char, 8> base64{};
    const uint8_t sample[3]{0xFB, 0xFF, 0xBF};
    ZOOM_CHECK(Base64UrlEncode(sample, 3, base64.data(), base64.size()) && std::string_view{base64.data()} == "-_-_" &&
                   Base64UrlEncode(sample, 2, base64.data(), base64.size()) && std::string_view{base64.data()} == "-_8",
               "base64url uses - and _ without padding");
    std::array<char, kMaximumUrlBytes> url{};
    ZOOM_CHECK(BuildAuthorizeUrl("zoom.us", "cl id", 48199, "CHALLENGE", "STATE", url.data(), url.size()) &&
                   std::string_view{url.data()} ==
                       "https://zoom.us/oauth/authorize?response_type=code&client_id=cl%20id&redirect_uri="
                       "http%3A%2F%2F127.0.0.1%3A48199%2Fredirect&code_challenge=CHALLENGE&code_challenge_method=S256&"
                       "state=STATE",
               "the authorize URL carries the PKCE challenge and the encoded loopback redirect");
    TokenSet tokens{};
    ZOOM_CHECK(
        SUCCEEDED(ParseTokenResponse(
            R"({"access_token":"AT","token_type":"bearer","refresh_token":"RT","expires_in":3600})", 1000, tokens)) &&
            tokens.AccessToken() == "AT" && tokens.RefreshToken() == "RT" && tokens.expiresAt == 4600,
        "a token response parses");
    ZOOM_CHECK(SUCCEEDED(ParseTokenResponse(R"({"access_token":"AT2","expires_in":60})", 5000, tokens)) &&
                   tokens.AccessToken() == "AT2" && tokens.RefreshToken() == "RT" && tokens.expiresAt == 5060,
               "a response without a refresh token keeps the previous one");
    ZOOM_CHECK(ParseTokenResponse(R"({"error":"invalid_grant"})", 0, tokens) == E_ACCESSDENIED,
               "an error response is E_ACCESSDENIED");
    ZOOM_CHECK(FAILED(ParseTokenResponse(R"({"refresh_token":"only"})", 0, tokens)),
               "a response without an access token is rejected");
    MemoryCredentialStore store;
    std::array<char, kMaximumTokenBytes + 1> read{};
    uint32_t bytes = 0;
    ZOOM_CHECK(store.Read("id", read.data(), read.size(), bytes) == S_FALSE && bytes == 0, "empty store reads S_FALSE");
    ZOOM_CHECK(SUCCEEDED(store.Write("id", "refresh")) && store.Read("id", read.data(), read.size(), bytes) == S_OK &&
                   std::string_view(read.data(), bytes) == "refresh",
               "a written credential reads back");
    ZOOM_CHECK(store.Delete("id") == S_OK && store.Delete("id") == S_FALSE, "delete is idempotent");
    return S_OK;
}

// Sends one HTTP GET to the listener from a client socket, the way a browser redirect does.
[[nodiscard]] bool SendRedirect(uint16_t port, const char* requestLine) noexcept
{
    const SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET)
    {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool ok = connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    if (ok)
    {
        std::string request = requestLine;
        request += "\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
        ok = send(client, request.c_str(), static_cast<int>(request.size()), 0) == static_cast<int>(request.size());
    }
    // The listener answers only when its owner pumps it, so the client never waits for the reply: it half-closes
    // and leaves; the request bytes still arrive ahead of the FIN.
    (void)shutdown(client, SD_SEND);
    closesocket(client);
    return ok;
}

[[nodiscard]] HRESULT TestLoopbackListener() noexcept
{
    using namespace Zoom;
    WSADATA data{};
    ZOOM_CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock");
    const auto cleanup = wil::scope_exit([]() noexcept { WSACleanup(); });
    LoopbackListener listener;
    ZOOM_CHECK(SUCCEEDED(listener.Start(0)) && listener.Running() && listener.Port() != 0 && listener.Event(),
               "the listener binds an ephemeral loopback port");
    std::array<char, kMaximumCodeBytes + 1> code{};
    ZOOM_CHECK(listener.Pump("STATE", code.data(), code.size()) == S_FALSE, "nothing pending yet");
    ZOOM_CHECK(SendRedirect(listener.Port(), "GET /redirect?code=first&state=WRONG HTTP/1.1"), "wrong-state redirect");
    HRESULT pumped = S_FALSE;
    // Pump for a while: the wrong state is answered with 400 and the listener keeps running.
    for (int tick = 0; tick < 50 && listener.Running(); ++tick)
    {
        if (WaitForSingleObject(listener.Event(), 10) == WAIT_OBJECT_0)
        {
            pumped = listener.Pump("STATE", code.data(), code.size());
        }
    }
    ZOOM_CHECK(pumped == S_FALSE, "a redirect with the wrong state keeps listening");
    ZOOM_CHECK(listener.Running() && code[0] == '\0', "the wrong-state code was not accepted");
    ZOOM_CHECK(SendRedirect(listener.Port(), "GET /redirect?state=STATE&code=the-code HTTP/1.1"), "good redirect");
    pumped = S_FALSE;
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       if (listener.Running() && WaitForSingleObject(listener.Event(), 50) == WAIT_OBJECT_0)
                       {
                           pumped = listener.Pump("STATE", code.data(), code.size());
                       }
                       return pumped == S_OK;
                   },
                   3000),
               "the redirect with the expected state delivers its code");
    ZOOM_CHECK(std::string_view{code.data()} == "the-code" && !listener.Running(),
               "the code is copied and the listener stops");
    return S_OK;
}

[[nodiscard]] HRESULT TestLocal() noexcept
{
    using namespace Zoom;
    bool wanted = false;
    ZOOM_CHECK(Local::WantsState("mute", "on", wanted) && wanted && Local::WantsState("video", "off", wanted) &&
                   !wanted && Local::WantsState("raiseHand", "on", wanted) &&
                   !Local::WantsState("mute", "toggle", wanted) && !Local::WantsState("share", "monitor", wanted) &&
                   !Local::WantsState("leave", "now", wanted),
               "on/off need the state; toggles and the rest do not");
    std::array<char, 128> uri{};
    ZOOM_CHECK(Local::BuildJoinUri(1234567890ULL, "abc-1", uri.data(), uri.size()) &&
                   std::string_view{uri.data()} == "zoommtg://zoom.us/join?confno=1234567890&pwd=abc-1",
               "join uri with a passcode");
    ZOOM_CHECK(Local::BuildJoinUri(987654321ULL, "", uri.data(), uri.size()) &&
                   std::string_view{uri.data()} == "zoommtg://zoom.us/join?confno=987654321",
               "join uri without a passcode");
    ZOOM_CHECK(!Local::BuildJoinUri(0, "", uri.data(), uri.size()) &&
                   !Local::BuildJoinUri(1234567890ULL, "a&b", uri.data(), uri.size()) &&
                   !Local::BuildJoinUri(1234567890ULL, "", uri.data(), 8),
               "no number, a reserved passcode character, or a short buffer refuse");

    // The accessible toolbar read and press against a window with the English 7.1.5 names.
    Settings settings{};
    std::array<char, 64> diagnostic{};
    ZOOM_CHECK(
        SUCCEEDED(ParseSettingsJson(R"json({"clientId":"abc"})json", settings, diagnostic.data(), diagnostic.size())),
        "default labels");
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto uninitialize = wil::scope_exit([]() noexcept { CoUninitialize(); });
    {
        MeetingWindow meeting({L"Unmute, currently muted, Ctrl+Alt+Shift+Q, Unmute my audio",
                               L"Start my video, Ctrl+Alt+Shift+V", L"Raise hand", L"End, Alt+Q"});
        ZOOM_CHECK(meeting.Handle() != nullptr, "the meeting stand-in window exists");
        Local::Toolbar toolbar{};
        ZOOM_CHECK(Local::ReadToolbar(meeting.Handle(), toolbar) == S_OK && toolbar.count == 4,
                   "four visible buttons read through MSAA");
        Local::MeetingState state{};
        Local::ReadState(toolbar, settings, state);
        ZOOM_CHECK(state.buttons == 4 && state.muted == Local::Tri::Yes && state.videoOn == Local::Tri::No &&
                       state.handRaised == Local::Tri::No,
                   "muted, video off, hand down read from the button names");
        ZOOM_CHECK(Local::FindButton(toolbar, "currently muted") == 0 && Local::FindButton(toolbar, "End,") == 3 &&
                       Local::FindButton(toolbar, "Leave,") == UINT32_MAX,
                   "buttons are found by label, case-insensitively");
        ZOOM_CHECK(SUCCEEDED(Local::Press(toolbar, 0)) &&
                       WaitUntil([&]() noexcept { return meeting.Clicks(0) == 1; }, 3000) && meeting.Clicks(1) == 0,
                   "the default action clicks the button");
        ZOOM_CHECK(Local::Press(toolbar, 4) == E_INVALIDARG, "an index past the toolbar is refused");
        meeting.SetCaption(0, L"Mute, currently unmuted, Ctrl+Alt+Shift+Q, Mute my audio");
        meeting.SetCaption(1, L"Stop my video, Ctrl+Alt+Shift+V");
        meeting.SetCaption(2, L"Lower hand");
        ZOOM_CHECK(Local::ReadToolbar(meeting.Handle(), toolbar) == S_OK, "re-read");
        Local::ReadState(toolbar, settings, state);
        ZOOM_CHECK(state.muted == Local::Tri::No && state.videoOn == Local::Tri::Yes &&
                       state.handRaised == Local::Tri::Yes,
                   "the opposite names read as the opposite states");
        meeting.SetCaption(0, L"Sonido");
        meeting.SetCaption(1, L"Vídeo");
        ZOOM_CHECK(Local::ReadToolbar(meeting.Handle(), toolbar) == S_OK, "re-read");
        Local::ReadState(toolbar, settings, state);
        ZOOM_CHECK(state.muted == Local::Tri::Unknown && state.videoOn == Local::Tri::Unknown &&
                       state.handRaised == Local::Tri::Yes,
                   "names outside the labels leave their state unknown");
        Settings spanish{};
        ZOOM_CHECK(
            SUCCEEDED(ParseSettingsJson(R"json({"mode":"local","labels":{"muted":"sonido","videoOn":"vídeo"}})json",
                                        spanish, diagnostic.data(), diagnostic.size())) &&
                spanish.mode == Mode::Local && spanish.clientIdBytes == 0,
            "labels override per language; mode local needs no client id");
        Local::ReadState(toolbar, spanish, state);
        ZOOM_CHECK(state.muted == Local::Tri::Yes && state.videoOn == Local::Tri::Yes,
                   "overridden labels match case-insensitively");
    }
    Local::Toolbar none{};
    ZOOM_CHECK(Local::ReadToolbar(nullptr, none) == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE),
               "a null window is refused");
    return S_OK;
}

[[nodiscard]] HRESULT TestShippedModule() noexcept
{
    std::array<wchar_t, 1024> path{};
    ZOOM_CHECK(SUCCEEDED(BuildSiblingPath(L"Plugins\\zoom.action.dll", path)), "plugin path");
    wil::unique_hmodule module{
        LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    ZOOM_CHECK(module != nullptr, "zoom.action.dll maps");
    const auto create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const auto enumerate = Resolve<RedXeEnumeratePluginsFn>(module.get(), kRedXeEnumeratePluginsExport);
    const auto contract =
        Resolve<RedXeGetPluginSettingsContractFn>(module.get(), kRedXeGetPluginSettingsContractExport);
    const auto actionContract = Resolve<RedXeGetActionContractFn>(module.get(), kRedXeGetActionContractExport);
    const auto diagnostics = Resolve<RedXeZoomGetTestDiagnosticsFn>(module.get(), kRedXeZoomGetTestDiagnosticsExport);
    const auto useSynthetic =
        Resolve<RedXeZoomUseSyntheticSessionFn>(module.get(), kRedXeZoomUseSyntheticSessionExport);
    const auto seed = Resolve<RedXeZoomSeedCredentialFn>(module.get(), kRedXeZoomSeedCredentialExport);
    const auto setHost = Resolve<RedXeZoomSyntheticSetHostFn>(module.get(), kRedXeZoomSyntheticSetHostExport);
    const auto drop =
        Resolve<RedXeZoomSyntheticDropConnectionFn>(module.get(), kRedXeZoomSyntheticDropConnectionExport);
    const auto counters = Resolve<RedXeZoomSyntheticCountersFn>(module.get(), kRedXeZoomSyntheticCountersExport);
    const auto stored = Resolve<RedXeZoomStoredCredentialFn>(module.get(), kRedXeZoomStoredCredentialExport);
    const auto setMeetingWindow = Resolve<RedXeZoomSetMeetingWindowFn>(module.get(), kRedXeZoomSetMeetingWindowExport);
    ZOOM_CHECK(create && enumerate && contract && actionContract && diagnostics && useSynthetic && seed && setHost &&
                   drop && counters && stored && setMeetingWindow,
               "required and test exports resolve");

    const RedXePluginMetadata* metadata = nullptr;
    uint32_t count = 0;
    ZOOM_CHECK(SUCCEEDED(enumerate(&metadata, &count)) && metadata && count == 1 &&
                   RedXeAsciiEqualsIgnoreCase(metadata[0].id, Zoom::kPluginId) &&
                   metadata[0].capabilities == (RedXePluginCapabilityService | RedXePluginCapabilityActions),
               "metadata: one service that publishes actions");
    const RedXePluginSettingsContract* settingsContract = nullptr;
    ZOOM_CHECK(SUCCEEDED(contract(Zoom::kPluginId, &settingsContract)) && settingsContract &&
                   settingsContract->sizeBytes == sizeof(RedXePluginSettingsContract),
               "settings contract");
    ZOOM_CHECK(contract("builtin.other", &settingsContract) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
               "unknown id has no settings contract");
    const RedXeActionContract* actions = nullptr;
    ZOOM_CHECK(SUCCEEDED(actionContract(Zoom::kPluginId, &actions)) && actions &&
                   actions->sizeBytes == sizeof(RedXeActionContract) && actions->namespaceCount == 1 &&
                   std::strcmp(actions->namespaces[0].name, "zoom") == 0 && actions->namespaces[0].actionCount >= 17,
               "the zoom action contract publishes one namespace");
    bool contractValid = true;
    bool sawMute = false;
    for (uint32_t index = 0; index < actions->namespaces[0].actionCount; ++index)
    {
        const RedXeActionDescriptor& descriptor = actions->namespaces[0].actions[index];
        contractValid = contractValid && descriptor.sizeBytes == sizeof(RedXeActionDescriptor) &&
                        RedXeIsActionNameSyntax(descriptor.name) && RedXeActionInNamespace(descriptor.name, "zoom") &&
                        (descriptor.flags & RedXeActionFlagDeferred) != 0 &&
                        (descriptor.targetKind != RedXeActionTargetEnum || descriptor.targetOptions);
        if (std::strcmp(descriptor.name, "zoom.mute") == 0)
        {
            sawMute = true;
            ZOOM_CHECK(RedXeActions::ValidateTarget(descriptor, "toggle") == S_OK &&
                           RedXeActions::ValidateTarget(descriptor, "maybe") == E_INVALIDARG,
                       "zoom.mute takes on, off, or toggle");
        }
        if (std::strcmp(descriptor.name, "zoom.leave") == 0)
        {
            ZOOM_CHECK(RedXeActions::ValidateTarget(descriptor, "") == E_INVALIDARG &&
                           RedXeActions::ValidateTarget(descriptor, "now") == S_OK,
                       "zoom.leave is destructive and needs now");
        }
    }
    ZOOM_CHECK(contractValid && sawMute, "every descriptor is well-formed and deferred");
    ZOOM_CHECK(actionContract("builtin.other", &actions) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
               "unknown id has no action contract");

    // Create the service with an envelope, as the host does.
    RedXeZoomTestDiagnostics report{};
    report.sizeBytes = sizeof(report);
    ZOOM_CHECK(diagnostics(&report) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "no service yet");
    TestHost host;
    constexpr char envelope[] =
        R"json({"plugin":{},"instance":{"clientId":"client-1","redirectPort":0,"domain":"zoom.us","displayName":"Desk","autoConnect":false}})json";
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    options.configurationJsonUtf8 = envelope;
    options.configurationBytes = static_cast<uint32_t>(sizeof(envelope) - 1);
    void* object = nullptr;
    ZOOM_CHECK(create(__uuidof(IRedXeService), &options, &host, Zoom::kPluginId, &object) ==
                   HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
               "redirectPort 0 rejects creation");
    constexpr char goodEnvelope[] =
        R"json({"plugin":{},"instance":{"clientId":"client-1","redirectPort":48199,"domain":"zoom.us","displayName":"Desk","autoConnect":false}})json";
    options.configurationJsonUtf8 = goodEnvelope;
    options.configurationBytes = static_cast<uint32_t>(sizeof(goodEnvelope) - 1);
    ZOOM_CHECK(create(__uuidof(IRedXeWidget), &options, &host, Zoom::kPluginId, &object) == E_NOINTERFACE,
               "the service refuses the widget IID");
    ZOOM_CHECK(SUCCEEDED(create(__uuidof(IRedXeService), &options, &host, Zoom::kPluginId, &object)) && object,
               "service creates");
    wil::com_ptr_nothrow<IRedXeService> service;
    service.attach(static_cast<IRedXeService*>(object));
    wil::com_ptr_nothrow<IRedXeDeviceWorker> worker;
    wil::com_ptr_nothrow<IRedXeActionPack> pack;
    wil::com_ptr_nothrow<IUnknown> identity;
    wil::com_ptr_nothrow<IUnknown> packIdentity;
    ZOOM_CHECK(SUCCEEDED(service.query_to(worker.put())) && SUCCEEDED(service.query_to(pack.put())) &&
                   SUCCEEDED(service.query_to(identity.put())) && SUCCEEDED(pack.query_to(packIdentity.put())) &&
                   identity.get() == packIdentity.get(),
               "the service exposes the device worker and the action pack on one identity");
    ZOOM_CHECK(SUCCEEDED(useSynthetic(TRUE)) && SUCCEEDED(seed("")), "synthetic session selected, no credential");

    RedXeServiceStartContext start{};
    start.sizeBytes = sizeof(start);
    start.flags = RedXeServiceFlagDeviceAccessDisabled;
    ZOOM_CHECK(SUCCEEDED(service->Start(&start)), "service starts without device access");
    ZOOM_CHECK(SUCCEEDED(diagnostics(&report)) && report.serviceStarted == 1 && report.deviceAccess == 0,
               "diagnostics after start");

    wil::unique_event_nothrow stop;
    wil::unique_event_nothrow wake;
    ZOOM_CHECK(SUCCEEDED(stop.create(wil::EventOptions::ManualReset)) && SUCCEEDED(wake.create()), "lane events");
    std::atomic<HRESULT> laneResult{E_PENDING};
    std::thread lane(
        [&]() noexcept
        {
            (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            laneResult.store(worker->RunDeviceWork(stop.get(), wake.get()), std::memory_order_release);
            CoUninitialize();
        });
    const auto stopLane = wil::scope_exit(
        [&]() noexcept
        {
            SetEvent(stop.get());
            if (lane.joinable())
            {
                lane.join();
            }
        });
    ZOOM_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.laneRunning == 1; }, 3000),
               "lane reports running");
    // sdkAvailable reflects whether the build linked the imported SDK; the synthetic session is used either way.
    ZOOM_CHECK(report.synthetic == 1 && report.credentialPresent == 0, "synthetic session, no credential");

    const auto execute = [&](const char* action, const char* target) noexcept
    {
        RedXeActionRequest request{};
        request.sizeBytes = sizeof(request);
        request.flags = RedXeActionRequestFlagDeviceAccessDisabled;
        request.actionUtf8 = action;
        request.targetUtf8 = target;
        return pack->Execute(&request);
    };
    RedXeActionRequest bad{};
    ZOOM_CHECK(pack->Execute(nullptr) == E_POINTER && pack->Execute(&bad) == E_INVALIDARG, "bad requests");
    ZOOM_CHECK(execute("page.next", nullptr) == E_INVALIDARG, "another namespace is refused");

    // Without a credential, mode auto takes the local path; without a meeting window that path refuses.
    ZOOM_CHECK(execute("zoom.mute", "toggle") == S_FALSE, "a request is deferred to the lane");
    ZOOM_CHECK(
        WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.requestsFailed == 1; }, 3000) &&
            report.lastFailure == static_cast<int32_t>(E_NOT_VALID_STATE) && report.localMode == 1 &&
            report.localRequests == 1 && host.actions.load() == 0,
        "signed-out auto mode routes to the local path, which refuses outside a meeting");
    // Mode sdk never falls back: signed-out actions fail and log once.
    constexpr char sdkOnly[] =
        R"json({"clientId":"client-1","redirectPort":48199,"domain":"zoom.us","displayName":"Desk","autoConnect":false,"mode":"sdk"})json";
    ZOOM_CHECK(SUCCEEDED(service->ApplySettings(sdkOnly, static_cast<uint32_t>(sizeof(sdkOnly) - 1))),
               "mode sdk applies");
    ZOOM_CHECK(execute("zoom.mute", "toggle") == S_FALSE, "a request is deferred to the lane");
    ZOOM_CHECK(
        WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.requestsFailed == 2; }, 3000) &&
            report.lastFailure == static_cast<int32_t>(E_NOT_VALID_STATE) && host.SawEvent("zoom-signed-out") &&
            report.localRequests == 1,
        "signed-out actions fail in mode sdk and log once");
    constexpr char autoMode[] =
        R"json({"clientId":"client-1","redirectPort":48199,"domain":"zoom.us","displayName":"Desk","autoConnect":false})json";
    ZOOM_CHECK(SUCCEEDED(service->ApplySettings(autoMode, static_cast<uint32_t>(sizeof(autoMode) - 1))),
               "mode auto applies again");

    // Sign in: the lane starts the listener; with device access disabled no browser opens. Deliver the redirect.
    ZOOM_CHECK(execute("zoom.signIn", nullptr) == S_FALSE, "sign-in queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.signingIn == 1; }, 3000) &&
                   report.listenerPort == 48199 && report.signIns == 1 && host.actions.load() == 0,
               "the loopback listener runs on the configured port and no browser launch happened");
    WSADATA data{};
    ZOOM_CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0, "winsock for the redirect");
    const auto cleanupWinsock = wil::scope_exit([]() noexcept { WSACleanup(); });
    // The synthetic transport turns the code into the tokens; the state must match, which the test cannot know, so
    // a wrong state is delivered first and must be ignored.
    ZOOM_CHECK(SendRedirect(48199, "GET /redirect?code=abc&state=nope HTTP/1.1"), "wrong-state redirect");
    Sleep(100);
    ZOOM_CHECK(SUCCEEDED(diagnostics(&report)) && report.signingIn == 1, "a wrong state keeps signing in");
    // Sign out cancels the pending sign-in so the rest of the test can seed a credential directly.
    ZOOM_CHECK(execute("zoom.signOut", "now") == S_FALSE, "sign-out queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.signingIn == 0; }, 3000),
               "sign-out stops the listener");

    // A seeded credential connects the synthetic client at the next action; the deferred request then runs.
    ZOOM_CHECK(SUCCEEDED(seed("refresh-1")), "credential seeded");
    constexpr char applied[] =
        R"json({"clientId":"client-1","redirectPort":48199,"domain":"zoom.us","displayName":"Desk","autoConnect":false})json";
    ZOOM_CHECK(SUCCEEDED(service->ApplySettings(applied, static_cast<uint32_t>(sizeof(applied) - 1))),
               "settings re-apply");
    ZOOM_CHECK(execute("zoom.start", nullptr) == S_FALSE, "start queued");
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       return SUCCEEDED(diagnostics(&report)) &&
                              report.meetingState == static_cast<uint32_t>(Zoom::MeetingState::InMeeting);
                   },
                   3000) &&
                   report.authState == static_cast<uint32_t>(Zoom::AuthState::Authenticated) &&
                   report.ipcState == static_cast<uint32_t>(Zoom::IpcState::Connected) && report.sessionStarts == 1 &&
                   report.credentialPresent == 1 && report.isHost == 1,
               "the deferred start connected and reached the meeting");
    std::array<char, 64> credential{};
    ZOOM_CHECK(SUCCEEDED(stored(credential.data(), static_cast<uint32_t>(credential.size()))) &&
                   std::string_view{credential.data()} == "refresh-1",
               "the refreshed credential is stored");

    // In-meeting actions against the synthetic client.
    const uint32_t executedBefore = report.requestsExecuted;
    ZOOM_CHECK(execute("zoom.mute", "on") == S_FALSE && execute("zoom.video", "toggle") == S_FALSE &&
                   execute("zoom.raiseHand", "toggle") == S_FALSE && execute("zoom.reaction", "clap") == S_FALSE,
               "four actions queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsExecuted >= executedBefore + 4; },
                         3000) &&
                   report.audioMuted == 1 && report.videoOn == 1 && report.handRaised == 1,
               "mute on, video toggled on, hand raised");
    uint64_t meetingNumber = 0;
    uint32_t reaction = 0;
    uint32_t chatMessages = 0;
    ZOOM_CHECK(SUCCEEDED(counters(&meetingNumber, &reaction, &chatMessages)) && reaction == 1, "clap is reaction 1");
    ZOOM_CHECK(execute("zoom.mute", "toggle") == S_FALSE && execute("zoom.chat.send", "hello") == S_FALSE &&
                   execute("zoom.share", "monitor@2") == S_FALSE && execute("zoom.record", "local.start") == S_FALSE,
               "more actions queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsExecuted >= executedBefore + 8; },
                         3000) &&
                   report.audioMuted == 0 && report.sharing == 1 && report.recording == 1,
               "toggle unmutes, share and record start");
    ZOOM_CHECK(SUCCEEDED(counters(&meetingNumber, &reaction, &chatMessages)) && chatMessages == 1, "chat sent");
    ZOOM_CHECK(execute("zoom.share", "stop") == S_FALSE && execute("zoom.record", "local.stop") == S_FALSE,
               "share and record stop queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsExecuted >= executedBefore + 10; },
                         3000) &&
                   report.sharing == 0 && report.recording == 0,
               "share and record stopped");

    // Role refusal: a participant cannot end for all or mute everyone.
    ZOOM_CHECK(SUCCEEDED(setHost(FALSE)), "become a participant");
    const uint32_t failedBefore = report.requestsFailed;
    ZOOM_CHECK(execute("zoom.participants.muteAll", nullptr) == S_FALSE, "muteAll queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsFailed == failedBefore + 1; },
                         3000) &&
                   report.lastFailure == static_cast<int32_t>(E_ACCESSDENIED),
               "a host-only action is refused for a participant");
    ZOOM_CHECK(execute("zoom.leave", "later") == S_FALSE, "leave with a wrong target queued");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.requestsFailed == failedBefore + 2; },
                         3000) &&
                   report.lastFailure == static_cast<int32_t>(E_INVALIDARG),
               "an unconfirmed leave is refused");
    ZOOM_CHECK(execute("zoom.leave", "now") == S_FALSE, "leave queued");
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       return SUCCEEDED(diagnostics(&report)) &&
                              report.meetingState == static_cast<uint32_t>(Zoom::MeetingState::Idle);
                   },
                   3000),
               "left the meeting");

    // Local mode: the meeting toolbar is read and pressed through its accessible objects, join goes through the
    // host's launch, and nothing touches the session.
    {
        MeetingWindow meeting({L"Unmute, currently muted, Ctrl+Alt+Shift+Q, Unmute my audio",
                               L"Start my video, Ctrl+Alt+Shift+V", L"Raise hand", L"End, Alt+Q"});
        ZOOM_CHECK(meeting.Handle() != nullptr && SUCCEEDED(setMeetingWindow(meeting.Handle())),
                   "the meeting stand-in is the local path's window");
        constexpr char local[] = R"json({"clientId":"client-1","redirectPort":48199,"mode":"local"})json";
        ZOOM_CHECK(SUCCEEDED(service->ApplySettings(local, static_cast<uint32_t>(sizeof(local) - 1))),
                   "mode local applies");
        ZOOM_CHECK(SUCCEEDED(diagnostics(&report)), "diagnostics before the local requests");
        const uint32_t hostActionsBefore = host.actions.load();
        const uint32_t modeLogsBefore = host.localModeLogs.load();
        const uint32_t localBefore = report.localRequests;
        const uint32_t submissionsBefore = report.sessionSubmissions;
        uint32_t executed = report.requestsExecuted;
        uint32_t failed = report.requestsFailed;
        // The counters move after the state is published, so waiting on them observes the state of that request.
        const auto executedOnce = [&]() noexcept
        {
            ++executed;
            return WaitUntil([&]() noexcept
                             { return SUCCEEDED(diagnostics(&report)) && report.requestsExecuted == executed; }, 5000);
        };
        const auto failedOnce = [&]() noexcept
        {
            ++failed;
            return WaitUntil([&]() noexcept
                             { return SUCCEEDED(diagnostics(&report)) && report.requestsFailed == failed; }, 5000);
        };
        // Already muted: on is satisfied without a press.
        ZOOM_CHECK(execute("zoom.mute", "on") == S_FALSE, "mute on queued");
        ZOOM_CHECK(executedOnce() && report.localRequests == localBefore + 1 && report.localMode == 1 &&
                       report.stateReads == 1 && report.audioMuted == 1 && report.videoOn == 0 &&
                       report.meetingState == static_cast<uint32_t>(Zoom::MeetingState::InMeeting) &&
                       meeting.Clicks(0) == 0 && host.localModeLogs.load() == modeLogsBefore + 1,
                   "mute on reads the toolbar, finds it muted, and presses nothing");
        // Muted but off wanted: the mute button is pressed.
        ZOOM_CHECK(execute("zoom.mute", "off") == S_FALSE, "mute off queued");
        ZOOM_CHECK(executedOnce() && WaitUntil([&]() noexcept { return meeting.Clicks(0) == 1; }, 3000) &&
                       report.stateReads == 2 && report.audioMuted == 0,
                   "mute off presses the mute button and expects unmuted");
        meeting.SetCaption(0, L"Mute, currently unmuted, Ctrl+Alt+Shift+Q, Mute my audio");
        // A toggle presses without checking the state.
        ZOOM_CHECK(execute("zoom.video", "toggle") == S_FALSE, "video toggle queued");
        ZOOM_CHECK(executedOnce() && WaitUntil([&]() noexcept { return meeting.Clicks(1) == 1; }, 3000) &&
                       meeting.Clicks(0) == 1,
                   "video toggle presses the video button");
        // Join goes to the Zoom client's URL handler through the host.
        ZOOM_CHECK(execute("zoom.join", "1234567890:abc") == S_FALSE, "join queued");
        ZOOM_CHECK(executedOnce() && host.actions.load() == hostActionsBefore + 1 &&
                       host.LastActionIs("system.launch") &&
                       host.LastTarget() == "zoommtg://zoom.us/join?confno=1234567890&pwd=abc",
                   "join launches the zoommtg uri");
        // Verbs without a local path are refused; a state the labels cannot read refuses on/off.
        ZOOM_CHECK(execute("zoom.reaction", "clap") == S_FALSE, "reaction queued");
        ZOOM_CHECK(failedOnce() && report.lastFailure == static_cast<int32_t>(E_NOTIMPL),
                   "a reaction has no local path");
        meeting.SetCaption(2, L"Main");
        ZOOM_CHECK(execute("zoom.raiseHand", "on") == S_FALSE, "hand on queued");
        ZOOM_CHECK(failedOnce() && report.lastFailure == static_cast<int32_t>(E_NOT_VALID_STATE) &&
                       host.stateUnknownLogs.load() == 1,
                   "an unreadable state refuses on/off and logs once");
        ZOOM_CHECK(execute("zoom.raiseHand", "off") == S_FALSE, "hand off queued");
        ZOOM_CHECK(failedOnce() && host.stateUnknownLogs.load() == 1, "the second unreadable state does not log again");
        // A host presses End when there is no Leave.
        ZOOM_CHECK(execute("zoom.leave", "now") == S_FALSE, "leave queued");
        ZOOM_CHECK(executedOnce() && WaitUntil([&]() noexcept { return meeting.Clicks(3) == 1; }, 3000),
                   "leave presses the End button");
        // Without a meeting window the path refuses.
        ZOOM_CHECK(SUCCEEDED(setMeetingWindow(nullptr)), "discovery restored");
        ZOOM_CHECK(execute("zoom.mute", "toggle") == S_FALSE, "mute toggle queued");
        ZOOM_CHECK(failedOnce() && report.lastFailure == static_cast<int32_t>(E_NOT_VALID_STATE),
                   "no meeting window, no press");
        ZOOM_CHECK(report.sessionSubmissions == submissionsBefore, "the session saw none of it");
        // Back to auto: the connected session serves again. The synthetic meeting was left earlier, so start one
        // through the session and leave it again.
        ZOOM_CHECK(SUCCEEDED(service->ApplySettings(autoMode, static_cast<uint32_t>(sizeof(autoMode) - 1))),
                   "mode auto applies again");
        ZOOM_CHECK(execute("zoom.start", nullptr) == S_FALSE, "start queued");
        ZOOM_CHECK(executedOnce() && report.sessionSubmissions == submissionsBefore + 1 && report.localMode == 0 &&
                       report.meetingState == static_cast<uint32_t>(Zoom::MeetingState::InMeeting),
                   "auto mode with a connected session goes back to the SDK");
        ZOOM_CHECK(execute("zoom.leave", "now") == S_FALSE, "leave queued");
        ZOOM_CHECK(executedOnce() && report.meetingState != static_cast<uint32_t>(Zoom::MeetingState::InMeeting),
                   "left the synthetic meeting again");
    }

    // A dropped connection reconnects with backoff.
    const uint32_t startsBefore = report.sessionStarts;
    ZOOM_CHECK(SUCCEEDED(drop()), "drop the client connection");
    ZOOM_CHECK(WaitUntil([&]() noexcept
                         { return SUCCEEDED(diagnostics(&report)) && report.sessionStarts == startsBefore + 1; },
                         5000) &&
                   report.reconnects == 1 && report.ipcState == static_cast<uint32_t>(Zoom::IpcState::Connected),
               "the lane reconnected after the drop");

    // Join with a meeting reference, then sign out deletes the credential.
    ZOOM_CHECK(execute("zoom.join", "https://zoom.us/j/1234567890?pwd=secret") == S_FALSE, "join queued");
    ZOOM_CHECK(
        WaitUntil(
            [&]() noexcept
            { return SUCCEEDED(counters(&meetingNumber, &reaction, &chatMessages)) && meetingNumber == 1234567890ULL; },
            3000),
        "the meeting number reached the session");
    ZOOM_CHECK(execute("zoom.signOut", "now") == S_FALSE, "sign-out queued");
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       return SUCCEEDED(stored(credential.data(), static_cast<uint32_t>(credential.size()))) &&
                              credential[0] == '\0';
                   },
                   3000) &&
                   SUCCEEDED(diagnostics(&report)) && report.credentialPresent == 0,
               "sign-out deleted the credential");

    // Stop: the lane returns within the drain budget and the service stops idempotently.
    SetEvent(stop.get());
    ZOOM_CHECK(WaitUntil([&]() noexcept { return laneResult.load(std::memory_order_acquire) != E_PENDING; },
                         kRedXeDeviceWorkerDrainMilliseconds) &&
                   SUCCEEDED(laneResult.load(std::memory_order_acquire)),
               "lane stopped within the drain budget");
    ZOOM_CHECK(SUCCEEDED(diagnostics(&report)) && report.laneRunning == 0, "lane reports stopped");
    ZOOM_CHECK(SUCCEEDED(service->Stop()) && SUCCEEDED(service->Stop()), "stop is idempotent");
    pack.reset();
    worker.reset();
    identity.reset();
    packIdentity.reset();
    service.reset();
    ZOOM_CHECK(diagnostics(&report) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "the service is gone after release");
    return S_OK;
}

// A host for the manual live run: the browser really opens for zoom.signIn; everything else is recorded.
class LiveHost final : public TestHost
{
  public:
    HRESULT STDMETHODCALLTYPE Log(const RedXeLogRecord* record) noexcept override
    {
        const HRESULT recorded = TestHost::Log(record);
        if (SUCCEEDED(recorded))
        {
            std::wprintf(L"[ log      ] %S: %S (0x%08X)\n", record->eventId,
                         record->messageUtf8 ? record->messageUtf8 : "", static_cast<unsigned>(record->code));
        }
        return recorded;
    }
    HRESULT STDMETHODCALLTYPE RequestAction(const RedXeActionRequest* request) noexcept override
    {
        const HRESULT recorded = TestHost::RequestAction(request);
        if (SUCCEEDED(recorded) && std::strcmp(request->actionUtf8, "system.launch") == 0 && request->targetUtf8)
        {
            std::array<wchar_t, 2048> url{};
            if (MultiByteToWideChar(CP_UTF8, 0, request->targetUtf8, -1, url.data(), static_cast<int>(url.size())) > 0)
            {
                std::wprintf(L"[ live     ] opening the browser for sign-in\n");
                (void)ShellExecuteW(nullptr, nullptr, url.data(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        return recorded;
    }
};

const wchar_t* StateName(uint32_t value, const wchar_t* const* names, size_t count) noexcept
{
    return value < count ? names[value] : L"?";
}

void PrintLive(const RedXeZoomTestDiagnostics& report) noexcept
{
    static constexpr const wchar_t* kAuth[] = {L"signedOut", L"authenticating", L"authenticated", L"failed"};
    static constexpr const wchar_t* kIpc[] = {L"disconnected", L"connecting", L"connected"};
    static constexpr const wchar_t* kMeeting[] = {L"idle", L"connecting", L"inMeeting", L"ending", L"failed"};
    std::wprintf(L"[ live     ] auth=%s ipc=%s meeting=%s audio=%u muted=%u video=%u sharing=%u hand=%u recording=%u "
                 L"host=%u | queued=%u executed=%u failed=%u dropped=%u submitted=%u completed=%u lastFailure=0x%08X "
                 L"lastAction=%S\n",
                 StateName(report.authState, kAuth, 4), StateName(report.ipcState, kIpc, 3),
                 StateName(report.meetingState, kMeeting, 5), report.audioJoined, report.audioMuted, report.videoOn,
                 report.sharing, report.handRaised, report.recording, report.isHost, report.requestsQueued,
                 report.requestsExecuted, report.requestsFailed, report.requestsDropped, report.sessionSubmissions,
                 report.sessionCompletions, static_cast<unsigned>(report.lastFailure), report.lastAction);
}

// Manual live validation against the installed Zoom Workplace client through the imported SDK. Never part of
// test.ps1: it opens a browser for the OAuth consent, starts a real meeting on the signed-in account, and drives
// every in-meeting action. Usage: ZoomTests.exe --live --client <marketplace client id> [--join <meeting>]
[[nodiscard]] HRESULT RunLive(const wchar_t* clientId, const wchar_t* join, bool local) noexcept
{
    std::array<wchar_t, 1024> path{};
    ZOOM_CHECK(SUCCEEDED(BuildSiblingPath(L"Plugins\\zoom.action.dll", path)), "plugin path");
    wil::unique_hmodule module{
        LoadLibraryExW(path.data(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
    ZOOM_CHECK(module != nullptr, "zoom.action.dll maps");
    const auto create = Resolve<RedXeCreateFn>(module.get(), kRedXeCreateExport);
    const auto diagnostics = Resolve<RedXeZoomGetTestDiagnosticsFn>(module.get(), kRedXeZoomGetTestDiagnosticsExport);
    ZOOM_CHECK(create && diagnostics, "exports resolve");

    std::array<char, 256> client{};
    ZOOM_CHECK(WideCharToMultiByte(CP_UTF8, 0, clientId, -1, client.data(), static_cast<int>(client.size()), nullptr,
                                   nullptr) > 0,
               "client id");
    std::string envelope = R"json({"plugin":{},"instance":{"clientId":")json";
    envelope += client.data();
    envelope += R"json(","redirectPort":48123,"domain":"zoom.us","displayName":"","autoConnect":false)json";
    envelope += local ? R"json(,"mode":"local"}})json" : R"json(}})json";
    LiveHost host;
    RedXeFactoryOptions options{};
    options.sizeBytes = sizeof(options);
    options.configurationJsonUtf8 = envelope.c_str();
    options.configurationBytes = static_cast<uint32_t>(envelope.size());
    void* object = nullptr;
    ZOOM_CHECK(SUCCEEDED(create(__uuidof(IRedXeService), &options, &host, Zoom::kPluginId, &object)) && object,
               "service creates");
    wil::com_ptr_nothrow<IRedXeService> service;
    service.attach(static_cast<IRedXeService*>(object));
    wil::com_ptr_nothrow<IRedXeDeviceWorker> worker;
    wil::com_ptr_nothrow<IRedXeActionPack> pack;
    ZOOM_CHECK(SUCCEEDED(service.query_to(worker.put())) && SUCCEEDED(service.query_to(pack.put())), "interfaces");

    RedXeServiceStartContext start{};
    start.sizeBytes = sizeof(start);
    start.flags = RedXeServiceFlagNone;
    ZOOM_CHECK(SUCCEEDED(service->Start(&start)), "service starts with device access");
    wil::unique_event_nothrow stop;
    wil::unique_event_nothrow wake;
    ZOOM_CHECK(SUCCEEDED(stop.create(wil::EventOptions::ManualReset)) && SUCCEEDED(wake.create()), "lane events");
    std::atomic<HRESULT> laneResult{E_PENDING};
    std::thread lane(
        [&]() noexcept
        {
            (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            laneResult.store(worker->RunDeviceWork(stop.get(), wake.get()), std::memory_order_release);
            CoUninitialize();
        });
    const auto stopLane = wil::scope_exit(
        [&]() noexcept
        {
            SetEvent(stop.get());
            if (lane.joinable())
            {
                lane.join();
            }
        });
    RedXeZoomTestDiagnostics report{};
    report.sizeBytes = sizeof(report);
    ZOOM_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.laneRunning == 1; }, 3000),
               "lane reports running");
    ZOOM_CHECK(report.synthetic == 0, "no synthetic session");
    std::wprintf(L"[ live     ] credential %s, sdk %s, mode %s\n", report.credentialPresent ? L"present" : L"absent",
                 report.sdkAvailable ? L"linked" : L"absent", local ? L"local" : L"auto");

    const auto execute = [&](const char* action, const char* target) noexcept
    {
        RedXeActionRequest request{};
        request.sizeBytes = sizeof(request);
        request.actionUtf8 = action;
        request.targetUtf8 = target;
        const HRESULT result = pack->Execute(&request);
        std::wprintf(L"[ live     ] %S %S -> 0x%08X\n", action, target ? target : "", static_cast<unsigned>(result));
        return result;
    };
    const auto settle = [&](DWORD milliseconds) noexcept
    {
        Sleep(milliseconds);
        (void)diagnostics(&report);
        PrintLive(report);
    };

    if (local)
    {
        // Local mode against the running client: the user starts (or joins) a meeting in Zoom; the driver then
        // reads the toolbar through its accessible objects and presses the buttons the verbs name.
        std::wprintf(L"[ live     ] start or join a meeting in the Zoom client (up to three minutes)\n");
        ZOOM_CHECK(WaitUntil([]() noexcept { return Zoom::Local::FindMeetingWindow() != nullptr; }, 3 * 60 * 1000),
                   "a Zoom meeting window exists");
        settle(1500);
        struct LocalStep final
        {
            const char* action;
            const char* target;
        };
        static constexpr std::array<LocalStep, 9> kLocalSteps{
            LocalStep{"zoom.mute", "on"},       LocalStep{"zoom.mute", "off"},    LocalStep{"zoom.mute", "toggle"},
            LocalStep{"zoom.mute", "toggle"},   LocalStep{"zoom.video", "off"},   LocalStep{"zoom.raiseHand", "on"},
            LocalStep{"zoom.raiseHand", "off"}, LocalStep{"zoom.focus", nullptr}, LocalStep{"zoom.mute", "off"},
        };
        uint32_t refusedLocal = 0;
        for (const LocalStep& step : kLocalSteps)
        {
            if (execute(step.action, step.target) != S_FALSE)
            {
                ++refusedLocal;
            }
            settle(2500);
        }
        std::wprintf(L"[ live     ] local steps=%zu refused=%u failed=%u stateReads=%u localRequests=%u\n",
                     kLocalSteps.size(), refusedLocal, report.requestsFailed, report.stateReads, report.localRequests);
        SetEvent(stop.get());
        ZOOM_CHECK(WaitUntil([&]() noexcept { return laneResult.load(std::memory_order_acquire) != E_PENDING; },
                             kRedXeDeviceWorkerDrainMilliseconds) &&
                       SUCCEEDED(laneResult.load(std::memory_order_acquire)),
                   "lane stopped within the drain budget");
        ZOOM_CHECK(SUCCEEDED(service->Stop()), "service stops");
        pack.reset();
        worker.reset();
        service.reset();
        return refusedLocal == 0 && report.requestsFailed == 0 ? S_OK : kTestFailure;
    }

    ZOOM_CHECK(report.sdkAvailable == 1, "the SDK-backed session is linked");
    if (!report.credentialPresent)
    {
        ZOOM_CHECK(execute("zoom.signIn", nullptr) == S_FALSE, "sign-in queued");
        std::wprintf(L"[ live     ] approve the sign-in in the browser (up to five minutes)\n");
        // The lane publishes signingIn once the listener is up; the wait then ends early when the service gives up
        // (listener port in use, exchange refused, timeout) instead of running the full five minutes.
        ZOOM_CHECK(WaitUntil([&]() noexcept { return SUCCEEDED(diagnostics(&report)) && report.signingIn == 1; }, 3000),
                   "the sign-in listener is up");
        ZOOM_CHECK(
            WaitUntil(
                [&]() noexcept
                { return SUCCEEDED(diagnostics(&report)) && (report.credentialPresent == 1 || report.signingIn == 0); },
                5 * 60 * 1000 + 5000) &&
                report.credentialPresent == 1,
            "sign-in completed and the credential is stored");
        settle(500);
    }

    // The first action connects the session; start (or join) the meeting once authenticated and connected.
    if (join && join[0] != L'\0')
    {
        std::array<char, 600> meeting{};
        ZOOM_CHECK(WideCharToMultiByte(CP_UTF8, 0, join, -1, meeting.data(), static_cast<int>(meeting.size()), nullptr,
                                       nullptr) > 0,
                   "meeting reference");
        ZOOM_CHECK(execute("zoom.join", meeting.data()) == S_FALSE, "join queued");
    }
    else
    {
        ZOOM_CHECK(execute("zoom.start", nullptr) == S_FALSE, "start queued");
    }
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       return SUCCEEDED(diagnostics(&report)) &&
                              report.authState == static_cast<uint32_t>(Zoom::AuthState::Authenticated) &&
                              report.ipcState == static_cast<uint32_t>(Zoom::IpcState::Connected);
                   },
                   30000),
               "authenticated and connected to the Zoom client");
    settle(500);
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       return SUCCEEDED(diagnostics(&report)) &&
                              report.meetingState == static_cast<uint32_t>(Zoom::MeetingState::InMeeting);
                   },
                   90000),
               "in meeting");
    settle(3000);

    struct Step final
    {
        const char* action;
        const char* target;
    };
    static constexpr std::array<Step, 15> kSteps{
        Step{"zoom.mute", "on"},
        Step{"zoom.mute", "off"},
        Step{"zoom.mute", "toggle"},
        Step{"zoom.video", "on"},
        Step{"zoom.video", "off"},
        Step{"zoom.raiseHand", "on"},
        Step{"zoom.raiseHand", "off"},
        Step{"zoom.reaction", "thumbsUp"},
        Step{"zoom.chat.send", "RedXe live validation"},
        Step{"zoom.share", "monitor"},
        Step{"zoom.share", "pause"},
        Step{"zoom.share", "resume"},
        Step{"zoom.share", "stop"},
        Step{"zoom.focus", nullptr},
        Step{"zoom.leave", "now"},
    };
    uint32_t refused = 0;
    for (const Step& step : kSteps)
    {
        if (execute(step.action, step.target) != S_FALSE)
        {
            ++refused;
        }
        settle(2500);
    }
    ZOOM_CHECK(WaitUntil(
                   [&]() noexcept
                   {
                       return SUCCEEDED(diagnostics(&report)) &&
                              report.meetingState != static_cast<uint32_t>(Zoom::MeetingState::InMeeting);
                   },
                   15000),
               "left the meeting");
    settle(500);
    std::wprintf(L"[ live     ] steps=%zu refused=%u failed=%u submitted=%u completed=%u\n", kSteps.size(), refused,
                 report.requestsFailed, report.sessionSubmissions, report.sessionCompletions);
    SetEvent(stop.get());
    ZOOM_CHECK(WaitUntil([&]() noexcept { return laneResult.load(std::memory_order_acquire) != E_PENDING; },
                         kRedXeDeviceWorkerDrainMilliseconds) &&
                   SUCCEEDED(laneResult.load(std::memory_order_acquire)),
               "lane stopped within the drain budget");
    ZOOM_CHECK(SUCCEEDED(service->Stop()), "service stops");
    pack.reset();
    worker.reset();
    service.reset();
    return refused == 0 && report.requestsFailed == 0 ? S_OK : kTestFailure;
}
} // namespace

int wmain(int argc, wchar_t** argv) noexcept
{
    const wchar_t* clientId = nullptr;
    const wchar_t* join = nullptr;
    bool live = false;
    bool local = false;
    for (int index = 1; index < argc; ++index)
    {
        if (wcscmp(argv[index], L"--live") == 0)
        {
            live = true;
        }
        else if (wcscmp(argv[index], L"--client") == 0 && index + 1 < argc)
        {
            clientId = argv[++index];
        }
        else if (wcscmp(argv[index], L"--join") == 0 && index + 1 < argc)
        {
            join = argv[++index];
        }
        else if (wcscmp(argv[index], L"--local") == 0)
        {
            local = true;
        }
    }
    if (live)
    {
        (void)setvbuf(stdout, nullptr, _IONBF, 0);
        if (!clientId && !local)
        {
            std::wprintf(L"usage: ZoomTests.exe --live (--client <marketplace client id> [--join <meeting>] | "
                         L"--local)\n");
            return 2;
        }
        const HRESULT result = RunLive(clientId ? clientId : L"none", join, local);
        std::wprintf(SUCCEEDED(result) ? L"Zoom live validation passed.\n" : L"Zoom live validation failed (0x%08X).\n",
                     static_cast<unsigned>(result));
        return SUCCEEDED(result) ? 0 : 1;
    }
    // Stream progress even when redirected, so a hang names the test it happened in.
    (void)setvbuf(stdout, nullptr, _IONBF, 0);
    struct Test final
    {
        const wchar_t* name;
        HRESULT (*run)() noexcept;
    };
    constexpr std::array tests{
        Test{L"settings model", &TestSettingsModel},       Test{L"auth material", &TestAuthMaterial},
        Test{L"loopback listener", &TestLoopbackListener}, Test{L"local path", &TestLocal},
        Test{L"shipped module", &TestShippedModule},
    };
    int failures = 0;
    for (const Test& test : tests)
    {
        std::wprintf(L"[ RUN      ] %s\n", test.name);
        const HRESULT result = test.run();
        if (FAILED(result))
        {
            ++failures;
            std::wprintf(L"[  FAILED  ] %s (0x%08X)\n", test.name, static_cast<unsigned>(result));
        }
        else
        {
            std::wprintf(L"[       OK ] %s\n", test.name);
        }
    }
    std::wprintf(failures == 0 ? L"ZoomTests passed.\n" : L"ZoomTests failed: %d.\n", failures);
    return failures == 0 ? 0 : 1;
}
