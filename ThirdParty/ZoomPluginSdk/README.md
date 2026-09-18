# Zoom Plugin SDK for Windows (developer import)

`zoom.action.dll` drives the Zoom Workplace client through the Zoom Plugin SDK for Windows. The SDK is licensed by
Zoom and downloaded from the App Marketplace by each developer; it is never committed. Without it the DLL builds
with the synthetic session only and logs `zoom-sdk-unavailable` at runtime.

1. Create a Marketplace **General app** with the Plugin SDK enabled and the redirect URL
   `http://127.0.0.1:48123/redirect` (or the `redirectPort` you configure).
2. Download `zoom-plugin-sdk-windows-7.1.0.2020` and extract it.
3. Run `.\ThirdParty\ZoomPluginSdk\Import-ZoomSdk.ps1 -PackageRoot <extracted directory>`.
4. `.\build.ps1` — the import props define `ZOOM_PLUGIN_SDK_AVAILABLE`, link the proxy delay-loaded, and copy its
   runtime to `<Plugins>\ZoomSdk\` (never beside `RedXe.exe`; the package ships its own CRT copies).
5. Optional live check against the running Zoom Workplace client:
   `.\.build\x64\Debug\ZoomTests.exe --live --client <marketplace client id>` (opens the browser for consent once).

`Plugins\Actions\Zoom\ZoomSdkSession.cpp` is pinned against package 7.1.0.2020; another version needs `-Force` and a
review of the adapter against its `export_h`.

Everything under this directory except `Import-ZoomSdk.ps1` and this file is ignored by git.
