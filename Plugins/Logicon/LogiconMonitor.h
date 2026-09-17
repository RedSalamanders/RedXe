#pragma once

// Debug-only monitor tile for the Logicon service: the nine key faces as the keypad shows them, live pressed state
// of every key and page button, the dialpad section, brightness, connection/feature diagnostics, the last HID++
// frames, and tap modes that press a key, push a color or test picture to it, or clear it.

#include "PlugInterfaces/Factory.h"

#include <windows.h>

namespace Logicon
{
[[nodiscard]] HRESULT CreateMonitorProvider(REFIID interfaceId, const RedXeFactoryOptions* options, IRedXeHost* host,
                                            void** result) noexcept;
}
