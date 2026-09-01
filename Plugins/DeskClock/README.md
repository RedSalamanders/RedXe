# Desk Clock rendering

Desk Clock uses DirectWrite during transactional device-resource initialization to rasterize the bounded time and
invariant-English date glyph set from the in-box Windows Bahnschrift SemiBold Condensed face, with Arial as the in-box
fallback. The resulting 1024×1024 single-channel immutable atlas and date advances are shared by the provider. The
system32 DirectWrite module is loaded with an isolated factory only while the atlas is built. All DirectWrite
interfaces, temporary CPU coverage, and the module handle are released before initialization returns, so inactive
discovery and the steady render path perform no font work or allocation.

The HLSL files are compiled to stripped Shader Model 5.0 bytecode by the Visual Studio project. The plugin performs no
runtime shader compilation or image decoding and carries no loose font or image asset.

The focused `DeskClockTests` executable validates the static factory contract, strict settings parsing, scheduled
cadence, deterministic rollover phases, WARP pixels, device recreation, and steady render allocations. Its Debug
and Release runs write deterministic BMP review frames beside the test executable; Release additionally writes the
target-display `DeskClock2560x720.bmp` frame under `.build/x64/Release/`.
