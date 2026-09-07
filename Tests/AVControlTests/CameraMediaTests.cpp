#include "../../Common/PlugInterfaces/FactoryImpl.h"
#include "Camera/CameraCapture.h"
#include "Camera/CameraController.h"
#include "Camera/CameraFrameChannel.h"
#include "Camera/CameraMediaSource.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <mfapi.h>
#include <mferror.h>
#include <sddl.h>
#include <stdexcept>
#include <thread>
#include <vector>
#include <wil/com.h>
#include <wil/resource.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")

namespace
{
using namespace AVControl::Camera;
uint32_t checks = 0;
void Check(bool value, const char* message)
{
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
class Frames final : public FrameProvider
{
  public:
    std::atomic<uint32_t> starts{0}, stops{0}, fills{0};
    std::atomic<bool> failed{false};
    BYTE luma = 235;
    HRESULT Start() noexcept override
    {
        ++starts;
        return S_OK;
    }
    void Stop() noexcept override
    {
        ++stops;
    }
    HRESULT Fill(BYTE* bytes, DWORD capacity, LONG pitch, LONGLONG) noexcept override
    {
        ++fills;
        if (pitch < static_cast<LONG>(FrameWidth) || static_cast<uint64_t>(pitch) * FrameHeight * 3 / 2 > capacity)
            return E_INVALIDARG;
        // Write a bright synthetic frame. A reported failure must clear even this partially produced frame.
        std::memset(bytes, luma, static_cast<size_t>(pitch) * FrameHeight);
        std::memset(bytes + static_cast<size_t>(pitch) * FrameHeight, 100,
                    static_cast<size_t>(pitch) * FrameHeight / 2);
        return failed ? E_ACCESSDENIED : S_OK;
    }
};
class Token final : public RedXeComObject<Token, IUnknown>
{
};
wil::com_ptr_nothrow<IMFMediaEvent> Event(IMFMediaEventGenerator* source, MediaEventType expected)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < end)
    {
        wil::com_ptr_nothrow<IMFMediaEvent> event;
        const HRESULT result = source->GetEvent(MF_EVENT_FLAG_NO_WAIT, event.put());
        if (result == MF_E_NO_EVENTS_AVAILABLE)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        Check(SUCCEEDED(result) && event, "camera source event is available within the fixture deadline");
        MediaEventType type{};
        HRESULT status = E_FAIL;
        Check(SUCCEEDED(event->GetType(&type)) && SUCCEEDED(event->GetStatus(&status)) && SUCCEEDED(status),
              "camera event status succeeds");
        Check(type == expected, "camera source orders its source/stream events");
        return event;
    }
    throw std::runtime_error("camera fixture event deadline expired");
}
wil::com_ptr_nothrow<IMFSample> Sample(IMFMediaStream* stream)
{
    auto event = Event(stream, MEMediaSample);
    wil::unique_prop_variant value;
    Check(SUCCEEDED(event->GetValue(&value)) && value.vt == VT_UNKNOWN && value.punkVal,
          "media sample event carries a COM sample");
    wil::com_ptr_nothrow<IMFSample> sample;
    Check(SUCCEEDED(value.punkVal->QueryInterface(IID_PPV_ARGS(sample.put()))), "media event uses standard IMFSample");
    return sample;
}
void CheckFrame(IMFSample* sample, BYTE luma, BYTE chroma)
{
    wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
    wil::com_ptr_nothrow<IMF2DBuffer2> plane;
    Check(SUCCEEDED(sample->GetBufferByIndex(0, buffer.put())) && SUCCEEDED(buffer.query_to(plane.put())),
          "camera frame uses standard MF 2D buffer");
    BYTE* bytes = nullptr;
    BYTE* start = nullptr;
    DWORD capacity = 0;
    LONG pitch = 0;
    Check(SUCCEEDED(plane->Lock2DSize(MF2DBuffer_LockFlags_Read, &bytes, &pitch, &start, &capacity)),
          "camera fixture locks output pixels");
    const auto unlock = wil::scope_exit([&] { (void)plane->Unlock2D(); });
    Check(pitch >= static_cast<LONG>(FrameWidth) && static_cast<uint64_t>(pitch) * FrameHeight * 3 / 2 <= capacity,
          "camera NV12 output is bounded and pitched");
    bool correct = true;
    for (UINT row = 0; row < FrameHeight * 3 / 2; ++row)
        for (LONG column = 0; column < pitch; ++column)
            correct =
                correct && bytes[static_cast<size_t>(row) * pitch + column] == (row < FrameHeight ? luma : chroma);
    Check(correct, "camera output fully replaces every pixel and padding byte");
}
} // namespace
namespace
{
struct CaptureCounters final
{
    std::atomic<uint32_t> opens{0}, closes{0}, frames{0};
    std::atomic<bool> active{false};
    std::atomic<HRESULT> failure{S_OK};
};
class SyntheticCapture final : public CaptureSession
{
  public:
    explicit SyntheticCapture(std::shared_ptr<CaptureCounters> counts) : _counts(std::move(counts)) {}
    HRESULT Open(const AVControl::DeviceId& id, HANDLE wake) noexcept override
    {
        ++_counts->opens;
        if (FAILED(_counts->failure.load()))
            return _counts->failure.load();
        try
        {
            auto frames = std::make_shared<Frames>();
            frames->luma = id.View() == "camera-one" ? BYTE{235} : BYTE{60};
            wil::com_ptr_nothrow<IMFMediaSource> source;
            HRESULT result = CreateMediaSource(frames, source.put());
            if (FAILED(result))
                return result;
            result = _reader.OpenSource(source.get(), wake);
            _counts->active = SUCCEEDED(result);
            return result;
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }
    }
    void Close() noexcept override
    {
        if (_reader.IsOpen())
            ++_counts->closes;
        _reader.Close();
        _counts->active = false;
    }
    HRESULT ReadFrame(std::span<BYTE> output, LONGLONG& timestamp) noexcept override
    {
        const HRESULT result = _reader.ReadFrame(output, timestamp);
        if (result == S_OK)
            ++_counts->frames;
        return result;
    }

  private:
    std::shared_ptr<CaptureCounters> _counts;
    CaptureReader _reader;
};
} // namespace
uint32_t RunCameraControllerTests()
{
    using namespace AVControl;
    checks = 0;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "camera controller fixture MTA");
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    Check(SUCCEEDED(MFStartup(MF_VERSION)), "camera controller fixture MF");
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    wil::unique_handle token;
    Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()) != FALSE, "controller fixture identity");
    alignas(TOKEN_USER) std::array<BYTE, sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE> tokenData{};
    DWORD needed = 0;
    Check(GetTokenInformation(token.get(), TokenUser, tokenData.data(), static_cast<DWORD>(tokenData.size()),
                              &needed) != FALSE,
          "controller token SID");
    wil::unique_hlocal_string sid;
    Check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenData.data())->User.Sid, sid.put()) != FALSE,
          "controller canonical SID");
    GUID guid{};
    BridgeIdentity identity;
    Check(SUCCEEDED(CoCreateGuid(&guid)) && MakeBridgeIdentity(sid.get(), identity, &guid) == S_OK,
          "controller isolates its route namespace");
    wil::unique_event changed(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    auto counts = std::make_shared<CaptureCounters>();
    CameraController controller(identity, std::make_unique<SyntheticCapture>(counts));
    Check(controller.Initialize(changed.get()) == S_OK, "controller initializes without a camera worker");
    DeviceId one, two;
    Check(one.Assign("camera-one") && two.Assign("camera-two"), "controller fixture source identities");
    auto state = controller.Snapshot();
    Check(controller.Select(one, state.revision) == S_OK && counts->opens == 0 && !controller.RequiresHelper(),
          "selecting an off source performs no capture or consumer-listener work");
    state = controller.Snapshot();
    Check(controller.Enable(true, state.revision - 1) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
          "controller rejects stale on intent");
    Check(controller.Enable(true, state.revision) == S_OK && counts->opens == 0 && controller.RequiresHelper(),
          "camera-on without consumers is armed without opening the physical source");
    BridgeFrameProvider consumer(identity);
    Check(consumer.Start() == S_OK, "controller consumer attaches to its virtual route");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Check(counts->opens == 0, "stream start without sample requests does not acquire a physical camera");
    std::vector<BYTE> output(static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2);
    auto drive = [&](BYTE expected)
    {
        bool found = false;
        const auto deadline = GetTickCount64() + 2000;
        while (!found && GetTickCount64() < deadline)
        {
            const HRESULT result =
                consumer.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime());
            consumer.EndFrame();
            found = result == S_OK && output.front() == expected && output[FrameWidth * FrameHeight - 1] == expected;
            if (!found)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Check(found, "camera controller publishes pixels from the selected source through its real reader and channel");
    };
    drive(235);
    Check(counts->opens == 1 && counts->active, "first sample demand acquires exactly one capture reader");
    const auto pauseDeadline = GetTickCount64() + 2000;
    while (counts->active && GetTickCount64() < pauseDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Check(!counts->active && counts->closes == 1 && controller.Snapshot().enabled && controller.RequiresHelper(),
          "stopped sample requests release capture while preserving the armed route and selected stream");
    const auto quietFrames = counts->frames.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(counts->frames == quietFrames, "paused camera demand creates no periodic capture work");
    drive(235);
    Check(counts->opens == 2, "resuming requests reacquires the selected source");
    state = controller.Snapshot();
    Check(controller.Select(two, state.revision) == S_OK, "source switching completes through the capture lane");
    drive(60);
    Check(controller.Snapshot().sourceId == two, "source switch reports its confirmed association");
    counts->failure = E_ACCESSDENIED;
    state = controller.Snapshot();
    Check(controller.Select(one, state.revision) == E_ACCESSDENIED,
          "capture privacy failure is reported without a success acknowledgement");
    state = controller.Snapshot();
    Check(!state.enabled && state.availability == Availability::AccessDenied && !counts->active,
          "failed source acquisition closes capture and reports privacy state");
    drive(16);
    counts->failure = S_OK;
    Check(controller.Enable(true, state.revision) == S_OK, "explicit retry after privacy failure is accepted");
    drive(235);
    state = controller.Snapshot();
    Check(controller.Enable(false, state.revision) == S_OK && !counts->active && !controller.RequiresHelper(),
          "confirmed off releases capture and retires the optional consumer worker");
    Check(FAILED(consumer.Fill(output.data(), static_cast<DWORD>(output.size()), FrameWidth, MFGetSystemTime())),
          "retired off route requests full neutral fallback from the media source");
    consumer.EndFrame();
    consumer.Stop();
    const auto opens = counts->opens.load();
    Check(controller.Select(two, controller.Snapshot().revision) == S_OK && counts->opens == opens,
          "profile source selection while off remains capture-free after a live session");
    controller.Shutdown();
    controller.Shutdown();
    Check(!counts->active, "controller shutdown is idempotent and leaves no capture");
    return checks;
}
uint32_t RunCameraCaptureTests()
{
    checks = 0;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "capture fixture MTA");
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    Check(SUCCEEDED(MFStartup(MF_VERSION)), "capture fixture starts real MF reader services");
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    wil::unique_event wake(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    Check(!!wake, "capture callback event");
    CaptureReader reader;
    auto frames = std::make_shared<Frames>();
    wil::com_ptr_nothrow<IMFMediaSource> source;
    Check(CreateMediaSource(frames, source.put()) == S_OK, "capture fixture uses an explicit synthetic media source");
    const auto cleanup = wil::scope_exit(
        [&]
        {
            reader.Close();
            if (source)
                (void)source->Shutdown();
        });
    Check(reader.OpenSource(source.get(), wake.get()) == S_OK && reader.IsOpen(),
          "asynchronous capture negotiates 720p NV12 through real Source Reader");
    Check(reader.OpenSource(nullptr, wake.get()) == E_INVALIDARG && reader.IsOpen(),
          "invalid capture input preserves the active reader");
    std::vector<BYTE> output(static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2);
    LONGLONG last = 0;
    for (uint32_t i = 0; i < 6; ++i)
    {
        HRESULT result = S_FALSE;
        LONGLONG timestamp = 0;
        const auto deadline = GetTickCount64() + 2000;
        while (result == S_FALSE && GetTickCount64() < deadline)
        {
            (void)WaitForSingleObject(wake.get(), 100);
            result = reader.ReadFrame(output, timestamp);
        }
        Check(result == S_OK, "capture delivers a complete asynchronous synthetic frame within deadline");
        Check(output.front() == 235 && output[FrameWidth * FrameHeight - 1] == 235 &&
                  output[FrameWidth * FrameHeight] == 100 && output.back() == 100,
              "capture copies both NV12 planes without retained padding");
        Check(timestamp > last, "capture frame receipts have monotonic QPC times");
        last = timestamp;
    }
    // One read is outstanding after consumption. Without consuming that completion no additional reads are
    // scheduled and the synthetic source stops producing: the adapter has a single-sample bound.
    (void)WaitForSingleObject(wake.get(), 500);
    const auto bounded = frames->fills.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(frames->fills == bounded, "capture does not queue reads or grow sample history behind a slow consumer");
    LONGLONG timestamp = 0;
    Check(reader.ReadFrame(std::span<BYTE>(output.data(), 10), timestamp) == E_INVALIDARG,
          "capture rejects an undersized destination");
    reader.Close();
    reader.Close();
    Check(!reader.IsOpen() && frames->stops == 1, "capture shutdown releases its source once and drains the session");
    Check(reader.ReadFrame(output, timestamp) == MF_E_SHUTDOWN, "closed reader cannot return a retained old frame");
    DWORD flags = 0;
    Check(source->GetCharacteristics(&flags) == MF_E_SHUTDOWN, "capture owns explicit media-source shutdown");
    // Close immediately after each new asynchronous request. A canceled callback belongs to its inactive
    // mailbox and may never access a replacement source's state or a destroyed wake handle.
    for (uint32_t i = 0; i < 8; ++i)
    {
        source.reset();
        Check(CreateMediaSource(frames, source.put()) == S_OK && reader.OpenSource(source.get(), wake.get()) == S_OK,
              "capture restarts with a new callback session");
        reader.Close();
    }
    Check(!reader.IsOpen(), "rapid capture replacement drains all readers");
    return checks;
}
uint32_t RunCameraMediaTests()
{
    checks = 0;
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "camera fixture initializes MTA");
    const auto com = wil::scope_exit([] { CoUninitialize(); });
    Check(SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL)),
          "camera fixture starts Media Foundation without opening devices");
    const auto mf = wil::scope_exit([] { (void)MFShutdown(); });
    const uint32_t before = ActiveMediaObjects();
    auto provider = std::make_shared<Frames>();
    wil::com_ptr_nothrow<IMFMediaSource> source;
    Check(CreateMediaSource(provider, source.put()) == S_OK,
          "standalone camera media source constructs without registration or physical camera");
    const auto cleanup = wil::scope_exit(
        [&]
        {
            if (source)
                (void)source->Shutdown();
        });
    Check(provider->starts == 0 && provider->fills == 0, "metadata construction creates no capture work");
    DWORD flags = 0;
    Check(source->GetCharacteristics(&flags) == S_OK && flags == MFMEDIASOURCE_IS_LIVE,
          "camera source is live and unseekable");
    Check(source->Pause() == MF_E_INVALID_STATE_TRANSITION, "camera source rejects pausing");
    wil::com_ptr_nothrow<IMFMediaSourceEx> extended;
    wil::com_ptr_nothrow<IMFSampleAllocatorControl> allocator;
    Check(SUCCEEDED(source.query_to(extended.put())) && SUCCEEDED(source.query_to(allocator.put())),
          "camera exposes Frame Server source and allocator mechanisms");
    DWORD input = 99;
    MFSampleAllocatorUsage usage{};
    Check(allocator->GetAllocatorUsage(0, &input, &usage) == S_OK && input == 0 &&
              usage == MFSampleAllocatorUsage_UsesProvidedAllocator,
          "Frame Server supplies the bounded sample allocator");
    Check(allocator->GetAllocatorUsage(1, &input, &usage) == MF_E_INVALIDSTREAMNUMBER,
          "camera rejects unknown output stream");
    wil::com_ptr_nothrow<IMFPresentationDescriptor> presentation;
    Check(source->CreatePresentationDescriptor(presentation.put()) == S_OK, "camera exports presentation descriptor");
    DWORD count = 0;
    BOOL selected = FALSE;
    wil::com_ptr_nothrow<IMFStreamDescriptor> descriptor;
    Check(presentation->GetStreamDescriptorCount(&count) == S_OK && count == 1 &&
              presentation->GetStreamDescriptorByIndex(0, &selected, descriptor.put()) == S_OK && selected,
          "camera presents one selected color stream");
    wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
    wil::com_ptr_nothrow<IMFMediaType> type;
    Check(descriptor->GetMediaTypeHandler(handler.put()) == S_OK && handler->GetCurrentMediaType(type.put()) == S_OK,
          "camera current media type is queryable before activation");
    UINT32 width = 0, height = 0;
    Check(MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &width, &height) == S_OK && width == 1280 && height == 720,
          "camera route negotiates its explicit bounded 720p format");
    PROPVARIANT position{};
    Check(source->Start(presentation.get(), &GUID_NULL, &position) == S_OK,
          "camera source starts its consumer lifetime");
    auto newStream = Event(source.get(), MENewStream);
    wil::unique_prop_variant streamValue;
    Check(newStream->GetValue(&streamValue) == S_OK && streamValue.vt == VT_UNKNOWN,
          "new-stream event carries standard stream object");
    wil::com_ptr_nothrow<IMFMediaStream> stream;
    Check(SUCCEEDED(streamValue.punkVal->QueryInterface(IID_PPV_ARGS(stream.put()))),
          "stream exposes inherited IMFMediaStream");
    (void)Event(source.get(), MESourceStarted);
    (void)Event(stream.get(), MEStreamStarted);
    Check(provider->starts == 1 && provider->fills == 0,
          "running source waits for a consumer request instead of periodic capture");
    wil::com_ptr_nothrow<IUnknown> token;
    token.attach(new Token());
    Check(stream->RequestSample(token.get()) == S_OK, "consumer request is queued with its token");
    auto first = Sample(stream.get());
    CheckFrame(first.get(), 235, 100);
    wil::com_ptr_nothrow<IUnknown> sampleToken;
    Check(first->GetUnknown(MFSampleExtension_Token, IID_PPV_ARGS(sampleToken.put())) == S_OK &&
              sampleToken.get() == token.get(),
          "camera preserves the consumer sample token");
    LONGLONG firstTime = 0, duration = 0;
    Check(first->GetSampleTime(&firstTime) == S_OK && firstTime > 0 && first->GetSampleDuration(&duration) == S_OK &&
              duration == FrameDuration,
          "camera samples carry QPC-based time and duration");
    first.reset();
    provider->failed = true;
    Check(stream->RequestSample(nullptr) == S_OK, "failed transport still accepts consumer cadence");
    auto off = Sample(stream.get());
    CheckFrame(off.get(), 16, 128);
    LONGLONG secondTime = 0;
    Check(off->GetSampleTime(&secondTime) == S_OK && secondTime >= firstTime + FrameDuration,
          "sample timestamps respect the 30 FPS ceiling");
    Check(FAILED(off->GetUnknown(MFSampleExtension_Token, IID_PPV_ARGS(sampleToken.put()))),
          "sample pool reuse clears an old request token");
    off.reset();
    const uint32_t idleFills = provider->fills;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Check(provider->fills == idleFills, "running camera with no sample requests has no periodic work");
    Check(source->Stop() == S_OK, "camera stops its consumer lifetime");
    (void)Event(stream.get(), MEStreamStopped);
    (void)Event(source.get(), MESourceStopped);
    Check(provider->stops == 1 && stream->RequestSample(nullptr) == MF_E_INVALIDREQUEST,
          "stopped camera releases transport and rejects sampling");
    Check(source->Start(presentation.get(), nullptr, &position) == S_OK,
          "camera can restart without re-registering the virtual device");
    (void)Event(source.get(), MEUpdatedStream);
    (void)Event(source.get(), MESourceStarted);
    (void)Event(stream.get(), MEStreamStarted);
    Check(source->Shutdown() == S_OK && source->Shutdown() == S_OK, "camera shutdown is idempotent");
    Check(stream->RequestSample(nullptr) == MF_E_SHUTDOWN && source->GetCharacteristics(&flags) == MF_E_SHUTDOWN,
          "retained external COM references fail safely after shutdown");
    Check(provider->starts == 2 && provider->stops == 2, "each media consumer lifetime stops exactly once");
    streamValue.reset();
    newStream.reset();
    stream.reset();
    extended.reset();
    allocator.reset();
    source.reset();
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (ActiveMediaObjects() != before && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Check(ActiveMediaObjects() == before, "camera source, stream and timer-session objects drain before module unload");
    return checks;
}

uint32_t RunCameraChannelTests()
{
    checks = 0;
    wil::unique_handle token;
    Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.put()) != FALSE,
          "camera IPC fixture reads its own identity");
    alignas(TOKEN_USER) std::array<BYTE, sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE> tokenBytes{};
    DWORD returned = 0;
    Check(GetTokenInformation(token.get(), TokenUser, tokenBytes.data(), static_cast<DWORD>(tokenBytes.size()),
                              &returned) != FALSE,
          "camera IPC fixture obtains a bounded owner SID");
    wil::unique_hlocal_string ownerSid;
    Check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(tokenBytes.data())->User.Sid, ownerSid.put()) != FALSE,
          "camera IPC fixture canonicalizes its SID");
    FrameChannel consumer, producer, rejected;
    Check(consumer.Create(ownerSid.get(), true) == S_OK,
          "camera test creates private local frame channel with explicit ACL");
    Check(rejected.Open(consumer.Names()) == E_INVALIDARG, "production channel admission rejects test-local names");
    auto mismatched = consumer.Names();
    const size_t length = wcslen(mismatched.mutex.data());
    mismatched.mutex[length - 2] = mismatched.mutex[length - 2] == L'0' ? L'1' : L'0';
    Check(rejected.Open(mismatched, true) == E_INVALIDARG,
          "camera frame mapping cannot be paired with a different lock");
    Check(producer.Open(consumer.Names(), true) == S_OK,
          "producer opens existing channel without creating global objects");
    ChannelState state;
    Check(producer.State(state) == S_OK && !state.enabled && state.revision == 1,
          "camera channel starts closed without an old image");
    constexpr size_t imageBytes = static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2;
    constexpr LONG pitch = FrameWidth + 16;
    std::vector<BYTE> physical(imageBytes, 201);
    std::vector<BYTE> output(static_cast<size_t>(pitch) * FrameHeight * 3 / 2, 0xff);
    const auto read = [&](LONGLONG now, bool live)
    {
        Check(consumer.Read(output.data(), static_cast<DWORD>(output.size()), pitch, now) == S_OK,
              "consumer takes frame-publication lease");
        const auto release = wil::scope_exit([&] { consumer.EndRead(); });
        bool correct = true;
        for (UINT row = 0; row < FrameHeight * 3 / 2; ++row)
            for (LONG column = 0; column < pitch; ++column)
            {
                const BYTE expected = live && column < static_cast<LONG>(FrameWidth) ? 201
                                      : row < FrameHeight                            ? 16
                                                                                     : 128;
                correct = correct && output[static_cast<size_t>(row) * pitch + column] == expected;
            }
        Check(correct, "channel produces current image or fully neutral NV12 with cleared padding");
    };
    read(1'000'000, false);
    Check(producer.Write(1, physical, 1'000'000) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
          "closed camera gate rejects physical frame publication");
    Check(producer.SetGate({2, true}) == S_OK, "dedicated camera-on opens a new revision");
    Check(producer.Write(2, physical, 1'000'000) == S_OK, "camera accepts one current physical frame");
    read(1'000'001, true);
    read(4'000'001, false);
    Check(producer.Write(2, physical, 999'999) == S_FALSE, "camera drops an out-of-order frame");
    Check(consumer.Read(output.data(), static_cast<DWORD>(output.size()), pitch, 1'000'001) == S_OK,
          "consumer holds its lease through sample publication");
    HRESULT offResult = S_OK;
    std::thread off([&] { offResult = producer.SetGate({3, false}); });
    off.join();
    Check(offResult == HRESULT_FROM_WIN32(ERROR_TIMEOUT),
          "Off never acknowledges while an older sample is still publishing");
    consumer.EndRead();
    Check(producer.SetGate({3, false}) == S_OK, "Off acknowledges after the outstanding publication lease ends");
    Check(producer.Write(2, physical, 2'000'000) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
          "late old-source callback cannot cross a newer off gate");
    read(2'000'001, false);
    Check(producer.SetGate({4, true}) == S_OK, "camera-on creates another fresh frame generation");
    read(2'000'001, false);
    Check(producer.Write(4, physical, 2'000'000) == S_OK, "new source frame becomes eligible");
    Check(producer.SetGate({5, true}) == S_OK, "source switch invalidates frames while preserving enabled intent");
    read(2'000'001, false);
    Check(producer.SetGate({4, true}) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH),
          "old source state cannot overwrite a newer binding");
    Check(producer.Write(5, std::span(physical).first(100), 3'000'000) == E_INVALIDARG,
          "frame transport rejects truncated input");
    Check(consumer.Read(output.data(), 10, pitch, 3'000'001) == E_INVALIDARG,
          "frame transport rejects undersized output before acquiring its gate");
    const auto names = consumer.Names();
    std::thread abandoned(
        [&]
        {
            wil::unique_handle mutex(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, names.mutex.data()));
            if (mutex)
                (void)WaitForSingleObject(mutex.get(), 1000);
            // Deliberately exit this fixture thread while owning only its test mutex.
        });
    abandoned.join();
    Check(producer.State(state) == HRESULT_FROM_WIN32(ERROR_ABANDONED_WAIT_0),
          "abandoned frame publication reports connection uncertainty");
    Check(producer.State(state) == S_OK && !state.enabled, "abandoned producer fails closed for subsequent consumers");
    producer.Close();
    consumer.Close();
    Check(FAILED(rejected.Open(names, true)), "last owner releases named frame objects without persistent recordings");
    return checks;
}
