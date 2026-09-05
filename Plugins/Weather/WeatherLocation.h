#pragma once
#include <windows.h>

// Runs the short-lived sibling helper only for an unresolved location. WinRT never enters Weather.dll.
[[nodiscard]] HRESULT WeatherLocateWithHelper(HANDLE cancelEvent, double& latitude, double& longitude,
                                              const wchar_t* testArgument = nullptr) noexcept;
