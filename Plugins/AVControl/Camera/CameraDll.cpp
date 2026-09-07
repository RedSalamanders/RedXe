#include "CameraActivation.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")

// Registration is an explicit installer operation. Loading this DLL never edits registration, starts a thread,
// opens a physical camera, or initializes Media Foundation under the loader lock.
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void** result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    return clsid == AVControl::Camera::CameraSourceClsid ? AVControl::Camera::CreateCameraClassFactory(iid, result)
                                                         : CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllCanUnloadNow()
{
    return AVControl::Camera::CanUnloadCameraDll();
}
