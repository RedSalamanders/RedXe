#pragma once
#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <wil/resource.h>

namespace AVControl
{
// Endpoint callbacks can arrive between observations. Retain exact scalar changes (including changes back to
// the original value), separately from mute, so a percent-rounded UI never commits a stale gesture.
class AudioRevisionState final
{
  public:
    struct Snapshot final
    {
        uint64_t level = 0, mute = 0;
    };
    [[nodiscard]] Snapshot Observe(float scalar, bool muted) noexcept
    {
        const auto lock = wil::AcquireSRWLockExclusive(&_lock);
        if (!std::isfinite(scalar) || scalar < 0 || scalar > 1) return {_levelRevision, _muteRevision};
        if (!_initialized || scalar != _scalar) ++_levelRevision;
        if (!_initialized || muted != _muted) ++_muteRevision;
        _scalar = scalar; _muted = muted; _initialized = true;
        return {_levelRevision, _muteRevision};
    }
    [[nodiscard]] Snapshot Current() noexcept
    {
        const auto lock = wil::AcquireSRWLockShared(&_lock);
        return {_levelRevision, _muteRevision};
    }
  private:
    SRWLOCK _lock = SRWLOCK_INIT;
    float _scalar = 0;
    bool _muted = false, _initialized = false;
    uint64_t _levelRevision = 0, _muteRevision = 0;
};
} // namespace AVControl
