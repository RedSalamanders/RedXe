- Move the DxUi pin from side-branch commit `616104f3` to DxUi main `f2e328a0` (API revision 4). Main carries DxUi's
  2 November delivery, what the old pin's four branch-only commits did, and submenu hover-timer cancellation on
  keyboard and UI Automation focus. Restore now refuses a pinned source whose own `Build/NativeBuildPolicy.targets`
  differs from RedXe's, so the policy hash in the build identity attests the copy that compiles the archive. One adapter
  change: DxUi now exposes `PageIndicator` to UI Automation as a slider whose value is the page and no longer names it
  "Page N of M", so AV Control names its indicator `Page`. AV Control's preparation callback only requests a frame,
  and the host rebinds its state after embedded preparation, so neither needs a change. x64 Debug builds with zero
  warnings and passes the full non-interactive gate. DxUi's own review still keeps main short of a production pin:
  its P0 (PRIOR-03) is unreachable from RedXe, but CTRL-01 drops supplementary characters typed as `VK_PACKET` into
  AV Control's profile-name field. DxUi's planned history rewrite will need a re-pin, and a production package of the
  merged commit is still due.
