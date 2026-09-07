#include "CameraMediaSource.h"
#include "../../../Common/PlugInterfaces/FactoryImpl.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <mfapi.h>
#include <mferror.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

namespace AVControl::Camera
{
namespace
{
std::atomic<uint32_t> objectCount{0};
struct Counted
{
    Counted() noexcept
    {
        ++objectCount;
    }
    ~Counted()
    {
        --objectCount;
    }
};
class Session final : public RedXeComObject<Session, IUnknown>, private Counted
{
};

HRESULT CreateType(IMFMediaType** result) noexcept
{
    wil::com_ptr_nothrow<IMFMediaType> type;
    RETURN_IF_FAILED(MFCreateMediaType(type.put()));
    RETURN_IF_FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    RETURN_IF_FAILED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_DEFAULT_STRIDE, FrameWidth));
    RETURN_IF_FAILED(MFSetAttributeSize(type.get(), MF_MT_FRAME_SIZE, FrameWidth, FrameHeight));
    RETURN_IF_FAILED(MFSetAttributeRatio(type.get(), MF_MT_FRAME_RATE, FrameRate, 1));
    RETURN_IF_FAILED(MFSetAttributeRatio(type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    *result = type.detach();
    return S_OK;
}
bool IsSupportedType(IMFMediaType* type) noexcept
{
    if (!type)
        return false;
    GUID major{}, subtype{};
    UINT32 width = 0, height = 0, numerator = 0, denominator = 0, interlace = 0;
    return SUCCEEDED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) && major == MFMediaType_Video &&
           SUCCEEDED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) && subtype == MFVideoFormat_NV12 &&
           SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) && width == FrameWidth &&
           height == FrameHeight && SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &numerator, &denominator)) &&
           numerator == FrameRate && denominator == 1 && SUCCEEDED(type->GetUINT32(MF_MT_INTERLACE_MODE, &interlace)) &&
           interlace == MFVideoInterlace_Progressive;
}
HRESULT Neutral(BYTE* bytes, DWORD capacity, LONG pitch) noexcept
{
    if (!bytes || pitch < static_cast<LONG>(FrameWidth) ||
        static_cast<uint64_t>(pitch) * FrameHeight * 3 / 2 > capacity)
        return E_INVALIDARG;
    // Limited-range black NV12. Clear padding too so no previous physical image survives an off/failure frame.
    std::memset(bytes, 16, static_cast<size_t>(pitch) * FrameHeight);
    std::memset(bytes + static_cast<size_t>(pitch) * FrameHeight, 128, static_cast<size_t>(pitch) * FrameHeight / 2);
    return S_OK;
}

class Stream final : public RedXeComObject<Stream, IMFMediaStream2, IMFAsyncCallback>, private Counted
{
  public:
    HRESULT Initialize(IMFMediaSource* parent, std::shared_ptr<FrameProvider> provider) noexcept
    {
        _parent = parent;
        _provider = std::move(provider);
        RETURN_IF_FAILED(MFCreateEventQueue(_events.put()));
        wil::com_ptr_nothrow<IMFMediaType> type;
        RETURN_IF_FAILED(CreateType(type.put()));
        auto* rawType = type.get();
        RETURN_IF_FAILED(MFCreateStreamDescriptor(0, 1, &rawType, _descriptor.put()));
        wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
        RETURN_IF_FAILED(_descriptor->GetMediaTypeHandler(handler.put()));
        RETURN_IF_FAILED(handler->SetCurrentMediaType(type.get()));
        RETURN_IF_FAILED(_descriptor->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE));
        RETURN_IF_FAILED(_descriptor->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0));
        RETURN_IF_FAILED(_descriptor->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color));
        RETURN_IF_FAILED(_descriptor->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1));
        DWORD queue = 0;
        RETURN_IF_FAILED(MFAllocateWorkQueueEx(MF_MULTITHREADED_WORKQUEUE, &queue));
        _workQueue.reset(queue);
        return S_OK;
    }
    ~Stream()
    {
        Shutdown();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override
    {
        return RedXeComObject::QueryInterface(iid == __uuidof(IMFMediaStream) || iid == __uuidof(IMFMediaEventGenerator)
                                                  ? __uuidof(IMFMediaStream2)
                                                  : iid,
                                              output);
    }
    HRESULT STDMETHODCALLTYPE GetEvent(DWORD flags, IMFMediaEvent** result) noexcept override
    {
        wil::com_ptr_nothrow<IMFMediaEventQueue> queue;
        {
            const auto lock = wil::AcquireSRWLockShared(&_lock);
            if (_shutdown)
                return MF_E_SHUTDOWN;
            queue = _events;
        }
        return queue->GetEvent(flags, result);
    }
    HRESULT STDMETHODCALLTYPE BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) noexcept override
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _events->BeginGetEvent(callback, state);
    }
    HRESULT STDMETHODCALLTYPE EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) noexcept override
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _events->EndGetEvent(result, event);
    }
    HRESULT STDMETHODCALLTYPE QueueEvent(MediaEventType type, REFGUID extended, HRESULT status,
                                         const PROPVARIANT* value) noexcept override
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _events->QueueEventParamVar(type, extended, status, value);
    }
    HRESULT STDMETHODCALLTYPE GetMediaSource(IMFMediaSource** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _parent.copy_to(result);
    }
    HRESULT STDMETHODCALLTYPE GetStreamDescriptor(IMFStreamDescriptor** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _descriptor.copy_to(result);
    }
    HRESULT STDMETHODCALLTYPE RequestSample(IUnknown* token) noexcept override
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        if (_state != MF_STREAM_STATE_RUNNING)
            return MF_E_INVALIDREQUEST;
        if (_count == _tokens.size())
            return MF_E_NOTACCEPTING;
        _tokens[(_head + _count) % _tokens.size()] = token;
        ++_count;
        const HRESULT result = Schedule();
        if (FAILED(result))
        {
            --_count;
            _tokens[(_head + _count) % _tokens.size()].reset();
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE SetStreamState(MF_STREAM_STATE state) noexcept override
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        if (state == _state)
            return S_OK;
        if (state == MF_STREAM_STATE_STOPPED)
            return StopLocked(false);
        return MF_E_INVALID_STATE_TRANSITION; // Start() establishes allocator, format and consumer session together.
    }
    HRESULT STDMETHODCALLTYPE GetStreamState(MF_STREAM_STATE* state) noexcept override
    {
        if (!state)
            return E_POINTER;
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        *state = _state;
        return S_OK;
    }
    HRESULT SetAllocator(IUnknown* allocator) noexcept
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        if (_state != MF_STREAM_STATE_STOPPED)
            return MF_E_INVALID_STATE_TRANSITION;
        if (!allocator)
            return E_POINTER;
        return allocator->QueryInterface(IID_PPV_ARGS(_allocator.put()));
    }
    HRESULT Start(IMFMediaType* type) noexcept
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        if (!IsSupportedType(type))
            return MF_E_INVALIDMEDIATYPE;
        if (_state == MF_STREAM_STATE_RUNNING)
            return S_OK;
        if (!_allocator)
            RETURN_IF_FAILED(MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(_allocator.put())));
        RETURN_IF_FAILED(_allocator->InitializeSampleAllocator(MaximumSamplePool, type));
        _session.attach(new (std::nothrow) Session());
        if (!_session)
        {
            (void)_allocator->UninitializeSampleAllocator();
            return E_OUTOFMEMORY;
        }
        const HRESULT started = _provider ? _provider->Start() : S_OK;
        if (FAILED(started))
        {
            _session.reset();
            (void)_allocator->UninitializeSampleAllocator();
            return started;
        }
        _state = MF_STREAM_STATE_RUNNING;
        _lastTimestamp = 0;
        PROPVARIANT time{};
        time.vt = VT_I8;
        time.hVal.QuadPart = MFGetSystemTime();
        return _events->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, &time);
    }
    HRESULT Stop() noexcept
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : StopLocked(true);
    }
    void Shutdown() noexcept
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (_shutdown)
            return;
        (void)StopLocked(false);
        _shutdown = true;
        if (_events)
            (void)_events->Shutdown();
        _events.reset();
        _parent.reset();
        _descriptor.reset();
        _allocator.reset();
        _provider.reset();
    }
    HRESULT STDMETHODCALLTYPE GetParameters(DWORD* flags, DWORD* queue) noexcept override
    {
        if (!flags || !queue)
            return E_POINTER;
        *flags = 0;
        *queue = _workQueue.get();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult* result) noexcept override
    {
        wil::com_ptr_nothrow<IUnknown> session;
        if (!result || FAILED(result->GetState(session.put())))
            return E_INVALIDARG;
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        // Canceling an MF timer does not drain an already delivered callback. Its immutable session identity
        // prevents an old callback from consuming a new session's token or clearing its timer.
        if (_shutdown || !_session || session.get() != static_cast<IUnknown*>(_session.get()))
            return S_OK;
        _scheduled = false;
        _workKey = 0;
        if (_state != MF_STREAM_STATE_RUNNING || _count == 0)
            return S_OK;
        auto token = std::move(_tokens[_head]);
        _head = (_head + 1) % _tokens.size();
        --_count;
        wil::com_ptr_nothrow<IMFSample> sample;
        HRESULT status = S_OK;
        {
            const auto releaseGate = wil::scope_exit(
                [&]
                {
                    if (_provider)
                        _provider->EndFrame();
                });
            status = FillSample(token.get(), sample.put());
            if (SUCCEEDED(status))
                status = _events->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.get());
        }
        if (FAILED(status))
            (void)_events->QueueEventParamVar(MEError, GUID_NULL, status, nullptr);
        if (_count)
            (void)Schedule();
        return S_OK;
    }

  private:
    SRWLOCK _lock = SRWLOCK_INIT;
    wil::com_ptr_nothrow<IMFMediaSource> _parent;
    wil::com_ptr_nothrow<IMFMediaEventQueue> _events;
    wil::com_ptr_nothrow<IMFStreamDescriptor> _descriptor;
    wil::com_ptr_nothrow<IMFVideoSampleAllocator> _allocator;
    wil::com_ptr_nothrow<Session> _session;
    std::shared_ptr<FrameProvider> _provider;
    std::array<wil::com_ptr_nothrow<IUnknown>, MaximumPendingSamples> _tokens{};
    size_t _head = 0, _count = 0;
    MF_STREAM_STATE _state = MF_STREAM_STATE_STOPPED;
    bool _shutdown = false, _scheduled = false;
    LONGLONG _lastTimestamp = 0;
    MFWORKITEM_KEY _workKey = 0;
    wil::unique_any<DWORD, decltype(&MFUnlockWorkQueue), MFUnlockWorkQueue> _workQueue;
    HRESULT Schedule() noexcept
    {
        if (_scheduled || !_count)
            return S_OK;
        const LONGLONG remaining = _lastTimestamp + FrameDuration - MFGetSystemTime();
        const LONGLONG delay = std::max<LONGLONG>(1, (remaining + 9999) / 10000);
        RETURN_IF_FAILED(MFScheduleWorkItem(this, _session.get(), -delay, &_workKey));
        _scheduled = true;
        return S_OK;
    }
    HRESULT StopLocked(bool notify) noexcept
    {
        if (_scheduled)
            (void)MFCancelWorkItem(_workKey);
        _scheduled = false;
        _workKey = 0;
        _session.reset();
        for (auto& token : _tokens)
            token.reset();
        _head = _count = 0;
        if (_state == MF_STREAM_STATE_RUNNING && _provider)
            _provider->Stop();
        _state = MF_STREAM_STATE_STOPPED;
        if (_allocator)
            (void)_allocator->UninitializeSampleAllocator();
        return notify && _events ? _events->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr) : S_OK;
    }
    HRESULT FillSample(IUnknown* token, IMFSample** result) noexcept
    {
        wil::com_ptr_nothrow<IMFSample> sample;
        RETURN_IF_FAILED(_allocator->AllocateSample(sample.put()));
        wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
        wil::com_ptr_nothrow<IMF2DBuffer2> plane;
        RETURN_IF_FAILED(sample->GetBufferByIndex(0, buffer.put()));
        RETURN_IF_FAILED(buffer.query_to(plane.put()));
        BYTE* scan = nullptr;
        BYTE* start = nullptr;
        DWORD bytes = 0;
        LONG pitch = 0;
        RETURN_IF_FAILED(plane->Lock2DSize(MF2DBuffer_LockFlags_Write, &scan, &pitch, &start, &bytes));
        const auto unlock = wil::scope_exit([&] { (void)plane->Unlock2D(); });
        if (!scan || !start || scan < start || static_cast<uint64_t>(scan - start) > bytes)
            return E_UNEXPECTED;
        const DWORD capacity = bytes - static_cast<DWORD>(scan - start);
        const LONGLONG timestamp = std::max(MFGetSystemTime(), _lastTimestamp + FrameDuration);
        if (!_provider || FAILED(_provider->Fill(scan, capacity, pitch, timestamp)))
            RETURN_IF_FAILED(Neutral(scan, capacity, pitch));
        RETURN_IF_FAILED(sample->SetSampleTime(timestamp));
        RETURN_IF_FAILED(sample->SetSampleDuration(FrameDuration));
        RETURN_IF_FAILED(sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE));
        if (token)
            RETURN_IF_FAILED(sample->SetUnknown(MFSampleExtension_Token, token));
        else
            (void)sample->DeleteItem(MFSampleExtension_Token);
        _lastTimestamp = timestamp;
        *result = sample.detach();
        return S_OK;
    }
};

class Source final
    : public RedXeComObject<Source, IMFMediaSourceEx, IMFGetService, IKsControl, IMFSampleAllocatorControl>,
      private Counted
{
  public:
    HRESULT Initialize(std::shared_ptr<FrameProvider> provider) noexcept
    {
        RETURN_IF_FAILED(MFCreateEventQueue(_events.put()));
        RETURN_IF_FAILED(MFCreateAttributes(_attributes.put(), 3));
        RETURN_IF_FAILED(
            _attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
        _stream.attach(new (std::nothrow) Stream());
        if (!_stream)
            return E_OUTOFMEMORY;
        RETURN_IF_FAILED(_stream->Initialize(this, std::move(provider)));
        wil::com_ptr_nothrow<IMFStreamDescriptor> descriptor;
        RETURN_IF_FAILED(_stream->GetStreamDescriptor(descriptor.put()));
        auto* raw = descriptor.get();
        RETURN_IF_FAILED(MFCreatePresentationDescriptor(1, &raw, _presentation.put()));
        return _presentation->SelectStream(0);
    }
    ~Source()
    {
        (void)Shutdown();
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) noexcept override
    {
        return RedXeComObject::QueryInterface(iid == __uuidof(IMFMediaSource) || iid == __uuidof(IMFMediaEventGenerator)
                                                  ? __uuidof(IMFMediaSourceEx)
                                                  : iid,
                                              result);
    }
    HRESULT STDMETHODCALLTYPE GetEvent(DWORD flags, IMFMediaEvent** result) noexcept override
    {
        wil::com_ptr_nothrow<IMFMediaEventQueue> queue;
        {
            const auto lock = wil::AcquireSRWLockShared(&_lock);
            if (_shutdown)
                return MF_E_SHUTDOWN;
            queue = _events;
        }
        return queue->GetEvent(flags, result);
    }
    HRESULT STDMETHODCALLTYPE BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) noexcept override
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _events->BeginGetEvent(callback, state);
    }
    HRESULT STDMETHODCALLTYPE EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) noexcept override
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _events->EndGetEvent(result, event);
    }
    HRESULT STDMETHODCALLTYPE QueueEvent(MediaEventType type, REFGUID extended, HRESULT status,
                                         const PROPVARIANT* value) noexcept override
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _events->QueueEventParamVar(type, extended, status, value);
    }
    HRESULT STDMETHODCALLTYPE GetCharacteristics(DWORD* result) noexcept override
    {
        if (!result)
            return E_POINTER;
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        *result = MFMEDIASOURCE_IS_LIVE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreatePresentationDescriptor(IMFPresentationDescriptor** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _presentation->Clone(result);
    }
    HRESULT STDMETHODCALLTYPE Start(IMFPresentationDescriptor* presentation, const GUID* timeFormat,
                                    const PROPVARIANT* position) noexcept override
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        if (!presentation || !position)
            return E_INVALIDARG;
        if (timeFormat && *timeFormat != GUID_NULL)
            return MF_E_UNSUPPORTED_TIME_FORMAT;
        if (position->vt != VT_EMPTY && (position->vt != VT_I8 || position->hVal.QuadPart != 0))
            return MF_E_INVALIDREQUEST;
        DWORD count = 0, id = UINT_MAX;
        BOOL selected = FALSE;
        wil::com_ptr_nothrow<IMFStreamDescriptor> descriptor;
        wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
        wil::com_ptr_nothrow<IMFMediaType> type;
        RETURN_IF_FAILED(presentation->GetStreamDescriptorCount(&count));
        if (count != 1)
            return MF_E_INVALIDSTREAMNUMBER;
        RETURN_IF_FAILED(presentation->GetStreamDescriptorByIndex(0, &selected, descriptor.put()));
        RETURN_IF_FAILED(descriptor->GetStreamIdentifier(&id));
        if (!selected || id != 0)
            return MF_E_INVALIDSTREAMNUMBER;
        RETURN_IF_FAILED(descriptor->GetMediaTypeHandler(handler.put()));
        RETURN_IF_FAILED(handler->GetCurrentMediaType(type.put()));
        RETURN_IF_FAILED(_stream->Start(type.get()));
        RETURN_IF_FAILED(_events->QueueEventParamUnk(_everStarted ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK,
                                                     static_cast<IMFMediaStream2*>(_stream.get())));
        _everStarted = true;
        _started = true;
        PROPVARIANT time{};
        time.vt = VT_I8;
        time.hVal.QuadPart = MFGetSystemTime();
        return _events->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &time);
    }
    HRESULT STDMETHODCALLTYPE Stop() noexcept override
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        if (!_started)
            return S_OK;
        RETURN_IF_FAILED(_stream->Stop());
        _started = false;
        return _events->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
    }
    HRESULT STDMETHODCALLTYPE Pause() noexcept override
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : MF_E_INVALID_STATE_TRANSITION;
    }
    HRESULT STDMETHODCALLTYPE Shutdown() noexcept override
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (_shutdown)
            return S_OK;
        _shutdown = true;
        _started = false;
        if (_stream)
            _stream->Shutdown();
        if (_events)
            (void)_events->Shutdown();
        _stream.reset();
        _events.reset();
        _presentation.reset();
        _attributes.reset();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSourceAttributes(IMFAttributes** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : _attributes.copy_to(result);
    }
    HRESULT STDMETHODCALLTYPE GetStreamAttributes(DWORD id, IMFAttributes** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        if (id != 0)
            return MF_E_INVALIDSTREAMNUMBER;
        wil::com_ptr_nothrow<IMFStreamDescriptor> descriptor;
        RETURN_IF_FAILED(_stream->GetStreamDescriptor(descriptor.put()));
        return descriptor.query_to(result);
    }
    HRESULT STDMETHODCALLTYPE SetD3DManager(IUnknown*) noexcept override
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return _shutdown ? MF_E_SHUTDOWN : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetService(REFGUID, REFIID, void** result) noexcept override
    {
        if (!result)
            return E_POINTER;
        *result = nullptr;
        return MF_E_UNSUPPORTED_SERVICE;
    }
    HRESULT STDMETHODCALLTYPE KsProperty(PKSPROPERTY, ULONG, void*, ULONG, ULONG* returned) noexcept override
    {
        if (returned)
            *returned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE KsMethod(PKSMETHOD, ULONG, void*, ULONG, ULONG* returned) noexcept override
    {
        if (returned)
            *returned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE KsEvent(PKSEVENT, ULONG, void*, ULONG, ULONG* returned) noexcept override
    {
        if (returned)
            *returned = 0;
        return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
    }
    HRESULT STDMETHODCALLTYPE SetDefaultAllocator(DWORD id, IUnknown* allocator) noexcept override
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        return id == 0 ? _stream->SetAllocator(allocator) : MF_E_INVALIDSTREAMNUMBER;
    }
    HRESULT STDMETHODCALLTYPE GetAllocatorUsage(DWORD id, DWORD* input, MFSampleAllocatorUsage* usage) noexcept override
    {
        if (!input || !usage)
            return E_POINTER;
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        if (_shutdown)
            return MF_E_SHUTDOWN;
        if (id != 0)
            return MF_E_INVALIDSTREAMNUMBER;
        *input = 0;
        *usage = MFSampleAllocatorUsage_UsesProvidedAllocator;
        return S_OK;
    }

  private:
    SRWLOCK _lock = SRWLOCK_INIT;
    wil::com_ptr_nothrow<IMFMediaEventQueue> _events;
    wil::com_ptr_nothrow<IMFAttributes> _attributes;
    wil::com_ptr_nothrow<IMFPresentationDescriptor> _presentation;
    wil::com_ptr_nothrow<Stream> _stream;
    bool _shutdown = false, _started = false, _everStarted = false;
};
} // namespace
HRESULT CreateMediaSource(std::shared_ptr<FrameProvider> provider, IMFMediaSource** result) noexcept
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    wil::com_ptr_nothrow<Source> source;
    source.attach(new (std::nothrow) Source());
    if (!source)
        return E_OUTOFMEMORY;
    const HRESULT initialized = source->Initialize(std::move(provider));
    if (FAILED(initialized))
    {
        (void)source->Shutdown();
        return initialized;
    }
    return source.query_to(result);
}
uint32_t ActiveMediaObjects() noexcept
{
    return objectCount.load();
}
} // namespace AVControl::Camera
