#pragma once
#include "PlugInterfaces/Widget.h"
#include <DxUi/EmbeddedAccessibility.h>
#include <wil/com.h>

namespace AVControl
{
// Module-local C++ adaptation of the application's generation-bound COM site.
class AccessibilitySite final : public DxUi::EmbeddedAccessibilitySite
{
  public:
    explicit AccessibilitySite(IRedXeAccessibilitySite* site) noexcept : _site(site) {}
    HRESULT Navigate(NavigateDirection direction, IRawElementProviderFragment** result) noexcept override
    {
        return _site->Navigate(direction, result);
    }
    HRESULT FragmentRoot(IRawElementProviderFragmentRoot** result) noexcept override
    {
        return _site->GetFragmentRoot(result);
    }
    HRESULT RequestFocus() noexcept override
    {
        return _site->RequestFocus();
    }
    void ActionCompleted() noexcept override
    {
        _site->ActionCompleted();
    }

  private:
    wil::com_ptr_nothrow<IRedXeAccessibilitySite> _site;
};
} // namespace AVControl
