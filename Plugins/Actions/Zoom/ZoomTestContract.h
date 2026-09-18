#pragma once

// Bounded test-support exports of zoom.action.dll. Each export is declared here behind one macro so the shipped
// export set is readable from this header; the host never calls them. The synthetic session and the in-memory
// credential store let ZoomTests drive sign-in, connection, and every action without Zoom, a network, or the SDK.

#include <cstdint>
#include <windows.h>

#if defined(REDXE_PLUGIN_EXPORTS)
#define REDXE_ZOOM_TEST_API __declspec(dllexport)
#else
#define REDXE_ZOOM_TEST_API
#endif

// Snapshot of the service for tests. sizeBytes must equal sizeof.
struct RedXeZoomTestDiagnostics final
{
    uint32_t sizeBytes;
    uint32_t serviceStarted;
    uint32_t laneRunning;
    uint32_t deviceAccess;
    uint32_t sdkAvailable;
    uint32_t synthetic;
    uint32_t credentialPresent;
    uint32_t signingIn;
    // Zoom::AuthState / IpcState / MeetingState values.
    uint32_t authState;
    uint32_t ipcState;
    uint32_t meetingState;
    uint32_t audioJoined;
    uint32_t audioMuted;
    uint32_t videoOn;
    uint32_t sharing;
    uint32_t handRaised;
    uint32_t recording;
    uint32_t isHost;
    uint32_t requestsQueued;
    uint32_t requestsExecuted;
    uint32_t requestsFailed;
    uint32_t requestsDropped;
    uint32_t sessionStarts;
    uint32_t reconnects;
    uint32_t signIns;
    uint32_t listenerPort;
    uint32_t sessionSubmissions;
    uint32_t sessionCompletions;
    // Local path: whether the last request went through the client's accessible meeting controls, how many did,
    // and how many toolbar reads ran.
    uint32_t localMode;
    uint32_t localRequests;
    uint32_t stateReads;
    int32_t lastFailure;
    char lastAction[65];
};

static_assert(sizeof(RedXeZoomTestDiagnostics) == 196);

extern "C"
{
    // Fills diagnostics from the current service. ERROR_NOT_READY when no service object exists.
    REDXE_ZOOM_TEST_API HRESULT __stdcall RedXeZoomGetTestDiagnostics(RedXeZoomTestDiagnostics* diagnostics) noexcept;
    // Routes the lane to the in-memory synthetic session (TRUE) at its next start, with an in-memory credential
    // store and a transport that answers every token request with a fixed token set.
    REDXE_ZOOM_TEST_API HRESULT __stdcall RedXeZoomUseSyntheticSession(BOOL enabled) noexcept;
    // Seeds the in-memory credential store with a refresh token ("expired" makes the synthetic client reject the
    // access token the transport derives from it).
    REDXE_ZOOM_TEST_API HRESULT __stdcall RedXeZoomSeedCredential(const char* refreshToken) noexcept;
    // Synthetic client controls: host role, and dropping the IPC connection.
    REDXE_ZOOM_TEST_API HRESULT __stdcall RedXeZoomSyntheticSetHost(BOOL host) noexcept;
    REDXE_ZOOM_TEST_API HRESULT __stdcall RedXeZoomSyntheticDropConnection() noexcept;
    // Synthetic session counters: last meeting number joined or started, last reaction index, chat messages sent.
    REDXE_ZOOM_TEST_API HRESULT __stdcall RedXeZoomSyntheticCounters(uint64_t* meetingNumber, uint32_t* reaction,
                                                                     uint32_t* chatMessages) noexcept;
    // The refresh token the in-memory store holds (empty when none), for asserting sign-in and sign-out.
    REDXE_ZOOM_TEST_API HRESULT __stdcall RedXeZoomStoredCredential(char* refreshToken, uint32_t capacity) noexcept;
    // The window the local path treats as the Zoom meeting window (null restores discovery), so tests drive the
    // local route and the accessible toolbar read against a window of their own.
    REDXE_ZOOM_TEST_API HRESULT __stdcall RedXeZoomSetMeetingWindow(HWND window) noexcept;
}

using RedXeZoomGetTestDiagnosticsFn = decltype(&RedXeZoomGetTestDiagnostics);
using RedXeZoomUseSyntheticSessionFn = decltype(&RedXeZoomUseSyntheticSession);
using RedXeZoomSeedCredentialFn = decltype(&RedXeZoomSeedCredential);
using RedXeZoomSyntheticSetHostFn = decltype(&RedXeZoomSyntheticSetHost);
using RedXeZoomSyntheticDropConnectionFn = decltype(&RedXeZoomSyntheticDropConnection);
using RedXeZoomSyntheticCountersFn = decltype(&RedXeZoomSyntheticCounters);
using RedXeZoomStoredCredentialFn = decltype(&RedXeZoomStoredCredential);
using RedXeZoomSetMeetingWindowFn = decltype(&RedXeZoomSetMeetingWindow);

inline constexpr char kRedXeZoomGetTestDiagnosticsExport[] = "RedXeZoomGetTestDiagnostics";
inline constexpr char kRedXeZoomUseSyntheticSessionExport[] = "RedXeZoomUseSyntheticSession";
inline constexpr char kRedXeZoomSeedCredentialExport[] = "RedXeZoomSeedCredential";
inline constexpr char kRedXeZoomSyntheticSetHostExport[] = "RedXeZoomSyntheticSetHost";
inline constexpr char kRedXeZoomSyntheticDropConnectionExport[] = "RedXeZoomSyntheticDropConnection";
inline constexpr char kRedXeZoomSyntheticCountersExport[] = "RedXeZoomSyntheticCounters";
inline constexpr char kRedXeZoomStoredCredentialExport[] = "RedXeZoomStoredCredential";
inline constexpr char kRedXeZoomSetMeetingWindowExport[] = "RedXeZoomSetMeetingWindow";

#undef REDXE_ZOOM_TEST_API
