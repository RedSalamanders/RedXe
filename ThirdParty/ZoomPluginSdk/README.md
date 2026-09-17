# Zoom Plugin SDK for Windows (developer import)

`zoom.action.dll` drives the Zoom Workplace client through the Zoom Plugin SDK for Windows. The SDK is licensed by
Zoom and downloaded from the App Marketplace by each developer; it is never committed. Without it the DLL builds
with the synthetic session only and logs `zoom-sdk-unavailable` at runtime.

1. Create a Marketplace **General app** with the Plugin SDK enabled and the redirect URL
   `http://127.0.0.1:48123/redirect` (or the `redirectPort` you configure).
2. Download `zoom-plugin-sdk-windows-7.1.0.2020` and extract it.
3. Run `.\ThirdParty\ZoomPluginSdk\Import-ZoomSdk.ps1 -PackageRoot <extracted directory>`.
4. Pin `Plugins\Actions\Zoom\ZoomSdkSession.cpp` (every `ZOOM_SDK_PIN` marker) against `x64\export_h` and
   `demo\PSDKTestDlg.cpp`; the build stops at the markers until you do.
5. `.\build.ps1` — the import props define `ZOOM_PLUGIN_SDK_AVAILABLE`, link the proxy delay-loaded, and copy its
   runtime beside `RedXe.exe`.

Everything under this directory except `Import-ZoomSdk.ps1` and this file is ignored by git.
