#include "CameraCapture.h"
#include "../../../Common/PlugInterfaces/FactoryImpl.h"
#include <cstring>
#include <mfapi.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>

#pragma comment(lib, "mfreadwrite.lib")

namespace AVControl::Camera
{
namespace
{
constexpr size_t ImageBytes = static_cast<size_t>(FrameWidth) * FrameHeight * 3 / 2;
constexpr DWORD VideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD AllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
struct Mailbox final
{
    SRWLOCK lock = SRWLOCK_INIT;
    wil::com_ptr_nothrow<IMFSample> sample;
    wil::unique_handle wake;
    HRESULT status = S_OK;
    DWORD flags = 0;
    LONGLONG receivedAt = 0;
    bool ready = false, active = true;
};
class Callback final : public RedXeComObject<Callback, IMFSourceReaderCallback>
{
  public:
    explicit Callback(std::shared_ptr<Mailbox> box) noexcept : _box(std::move(box)) {}
    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG, IMFSample* sample) noexcept override
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_box->lock);
        if (!_box->active) return S_OK;
        _box->sample = sample; _box->status = status; _box->flags = flags;
        _box->receivedAt = MFGetSystemTime(); _box->ready = true;
        SetEvent(_box->wake.get()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent* event) noexcept override
    {
        HRESULT status = S_OK;
        if (event && SUCCEEDED(event->GetStatus(&status)) && FAILED(status)) return OnReadSample(status, 0, 0, 0, nullptr);
        return S_OK;
    }
  private:
    std::shared_ptr<Mailbox> _box;
};
HRESULT CopyRows(BYTE* scan, BYTE* start, DWORD capacity, LONG pitch, std::span<BYTE> output) noexcept
{
    if (!scan || !start || scan < start || pitch < static_cast<LONG>(FrameWidth) || output.size() != ImageBytes ||
        static_cast<uint64_t>(scan - start) + static_cast<uint64_t>(pitch) * FrameHeight * 3 / 2 > capacity) return MF_E_BUFFERTOOSMALL;
    for (UINT row = 0; row < FrameHeight * 3 / 2; ++row)
        std::memcpy(output.data() + static_cast<size_t>(row) * FrameWidth, scan + static_cast<size_t>(row) * pitch, FrameWidth);
    return S_OK;
}
}
struct CaptureReader::State final
{
    wil::com_ptr_nothrow<IMFMediaSource> source;
    wil::com_ptr_nothrow<IMFSourceReader> reader;
    std::shared_ptr<Mailbox> mailbox;
    LONG pitch = FrameWidth;
    void Close() noexcept
    {
        if (mailbox)
        {
            const auto lock = wil::AcquireSRWLockExclusive(&mailbox->lock);
            mailbox->active = false; mailbox->sample.reset(); mailbox->ready = false;
        }
        // These potentially driver-blocking operations are confined to the owned helper. Late callbacks keep
        // their inactive mailbox/event alive; they never retain a pointer to this State or its acquisition lane.
        if (source) (void)source->Shutdown();
        reader.reset(); source.reset(); mailbox.reset();
    }
    HRESULT Request() noexcept { return reader->ReadSample(VideoStream, 0, nullptr, nullptr, nullptr, nullptr); }
    HRESULT Copy(IMFSample* sample, std::span<BYTE> output) noexcept
    {
        DWORD count = 0; RETURN_IF_FAILED(sample->GetBufferCount(&count));
        if (count != 1) return MF_E_INVALIDMEDIATYPE; // Never allocate a per-frame contiguous conversion buffer.
        wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
        RETURN_IF_FAILED(sample->GetBufferByIndex(0, buffer.put()));
        wil::com_ptr_nothrow<IMF2DBuffer2> plane;
        BYTE* scan = nullptr; BYTE* start = nullptr; LONG actualPitch = pitch; DWORD capacity = 0;
        if (SUCCEEDED(buffer.query_to(plane.put())))
        {
            RETURN_IF_FAILED(plane->Lock2DSize(MF2DBuffer_LockFlags_Read, &scan, &actualPitch, &start, &capacity));
            const auto unlock = wil::scope_exit([&] { (void)plane->Unlock2D(); });
            return CopyRows(scan, start, capacity, actualPitch, output);
        }
        DWORD maximum = 0;
        RETURN_IF_FAILED(buffer->Lock(&start, &maximum, &capacity));
        const auto unlock = wil::scope_exit([&] { (void)buffer->Unlock(); });
        if (capacity > maximum) return MF_E_BUFFERTOOSMALL;
        return CopyRows(start, start, capacity, pitch, output);
    }
};
CaptureReader::CaptureReader() : _state(std::make_unique<State>()) {}
CaptureReader::~CaptureReader() { Close(); }
void CaptureReader::Close() noexcept { _state->Close(); }
bool CaptureReader::IsOpen() const noexcept { return !!_state->reader; }
HRESULT CaptureReader::OpenDevice(PCWSTR symbolicLink, HANDLE wake) noexcept
{
    if (!symbolicLink || !symbolicLink[0] || wcsnlen_s(symbolicLink, 1025) >= 1025) return E_INVALIDARG;
    Close();
    wil::com_ptr_nothrow<IMFAttributes> attributes;
    RETURN_IF_FAILED(MFCreateAttributes(attributes.put(), 2));
    RETURN_IF_FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    RETURN_IF_FAILED(attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symbolicLink));
    wil::com_ptr_nothrow<IMFMediaSource> source;
    RETURN_IF_FAILED(MFCreateDeviceSource(attributes.get(), source.put()));
    const auto cleanup = wil::scope_exit([&] { if (!_state->source && source) (void)source->Shutdown(); });
    return OpenSource(source.get(), wake);
}
HRESULT CaptureReader::OpenSource(IMFMediaSource* source, HANDLE wake) noexcept
{
    if (!source || !wake || wake == INVALID_HANDLE_VALUE) return E_INVALIDARG;
    Close(); _state->source = source;
    auto failed = wil::scope_exit([&] { Close(); });
    try { _state->mailbox = std::make_shared<Mailbox>(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    RETURN_IF_WIN32_BOOL_FALSE(DuplicateHandle(GetCurrentProcess(), wake, GetCurrentProcess(), _state->mailbox->wake.put(),
        EVENT_MODIFY_STATE, FALSE, 0));
    wil::com_ptr_nothrow<Callback> callback;
    callback.attach(new (std::nothrow) Callback(_state->mailbox));
    if (!callback) return E_OUTOFMEMORY;
    wil::com_ptr_nothrow<IMFAttributes> attributes;
    RETURN_IF_FAILED(MFCreateAttributes(attributes.put(), 3));
    RETURN_IF_FAILED(attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.get()));
    RETURN_IF_FAILED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE));
    RETURN_IF_FAILED(attributes->SetUINT32(MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE));
    RETURN_IF_FAILED(MFCreateSourceReaderFromMediaSource(source, attributes.get(), _state->reader.put()));
    RETURN_IF_FAILED(_state->reader->SetStreamSelection(AllStreams, FALSE));
    RETURN_IF_FAILED(_state->reader->SetStreamSelection(VideoStream, TRUE));
    wil::com_ptr_nothrow<IMFMediaType> type;
    RETURN_IF_FAILED(MFCreateMediaType(type.put()));
    RETURN_IF_FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    RETURN_IF_FAILED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    RETURN_IF_FAILED(MFSetAttributeSize(type.get(), MF_MT_FRAME_SIZE, FrameWidth, FrameHeight));
    RETURN_IF_FAILED(MFSetAttributeRatio(type.get(), MF_MT_FRAME_RATE, FrameRate, 1));
    RETURN_IF_FAILED(MFSetAttributeRatio(type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    RETURN_IF_FAILED(_state->reader->SetCurrentMediaType(VideoStream, nullptr, type.get()));
    type.reset(); RETURN_IF_FAILED(_state->reader->GetCurrentMediaType(VideoStream, type.put()));
    GUID subtype{}; UINT32 width = 0, height = 0, numerator = 0, denominator = 0, stride = FrameWidth;
    RETURN_IF_FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype));
    RETURN_IF_FAILED(MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &width, &height));
    RETURN_IF_FAILED(MFGetAttributeRatio(type.get(), MF_MT_FRAME_RATE, &numerator, &denominator));
    if (subtype != MFVideoFormat_NV12 || width != FrameWidth || height != FrameHeight || !denominator ||
        static_cast<uint64_t>(numerator) != static_cast<uint64_t>(FrameRate) * denominator) return MF_E_INVALIDMEDIATYPE;
    (void)type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride);
    if (stride < FrameWidth || stride > 16384) return MF_E_INVALIDMEDIATYPE;
    _state->pitch = static_cast<LONG>(stride);
    RETURN_IF_FAILED(_state->Request());
    failed.release(); return S_OK;
}
HRESULT CaptureReader::ReadFrame(std::span<BYTE> output, LONGLONG& timestamp) noexcept
{
    timestamp = 0;
    if (output.size() != ImageBytes) return E_INVALIDARG;
    if (!_state->reader || !_state->mailbox) return MF_E_SHUTDOWN;
    wil::com_ptr_nothrow<IMFSample> sample;
    HRESULT status = S_OK; DWORD flags = 0;
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_state->mailbox->lock);
        if (!_state->mailbox->ready) return S_FALSE;
        sample = std::move(_state->mailbox->sample); status = _state->mailbox->status; flags = _state->mailbox->flags;
        timestamp = _state->mailbox->receivedAt; _state->mailbox->ready = false;
    }
    RETURN_IF_FAILED(status);
    if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)) return MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED;
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) return MF_E_INVALIDMEDIATYPE;
    if (sample) RETURN_IF_FAILED(_state->Copy(sample.get(), output));
    RETURN_IF_FAILED(_state->Request());
    return sample ? S_OK : S_FALSE;
}
} // namespace AVControl::Camera
