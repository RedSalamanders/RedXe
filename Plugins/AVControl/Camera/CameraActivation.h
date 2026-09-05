#pragma once
#include <mfidl.h>

namespace AVControl::Camera
{
// Stable identity for the installed software source. These are not physical device IDs.
inline constexpr GUID CameraSourceClsid{0x10f8f1a2, 0x4a82, 0x4d82, {0x9c, 0x0e, 0xe2, 0x53, 0xb1, 0x51, 0xbd, 0xcb}};
inline constexpr wchar_t CameraSourceClsidString[] = L"{10F8F1A2-4A82-4D82-9C0E-E253B151BDCB}";
inline constexpr GUID CameraOwnerSidAttribute{0x529cdedc, 0x6daa, 0x43d1, {0x89, 0x50, 0x41, 0x4c, 0x51, 0x76, 0x44, 0xda}};
HRESULT CreateCameraClassFactory(REFIID iid, void** result) noexcept;
HRESULT CanUnloadCameraDll() noexcept;
} // namespace AVControl::Camera
