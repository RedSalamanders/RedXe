- Restores read vcpkg from complete clones again. Since 2026-11-18 every fresh restore failed inside
  `vcpkg install` with "Could not resolve host: github.com" for a promisor object. vcpkg 2026-07-27 extracts
  each versioned port with `git read-tree` and `git checkout-index` in an environment that holds only
  `GIT_INDEX_FILE`, where Git resolves no host, so it cannot fetch a blob that a `--filter=blob:none` clone
  left on the server. That blob was first needed when upstream microsoft/vcpkg de36db0455 moved
  `vcpkg-cmake` on `master` away from the tree the baseline selects.
  - `Build/VcpkgRestore.psm1` clones the executable and catalogue checkouts completely, fetches a missing pin
    with its history instead of `--depth 1`, and completes a partial or shallow clone from an earlier restore
    in place, keeping its HEAD and working tree.
  - `restore-dxui.ps1` calls the new `Build/VcpkgToolCheckout.psm1` (the same file in every DxUi consumer)
    before the pinned DxUi's `vcpkg-install.ps1`. The call gives that installer a complete checkout of its own
    vcpkg pin, which the installer reuses instead of its partial clone. The DxUi pin is unchanged; DxUi's own
    installer is fixed in DxUi pull request 267.
  - Validation: `VcpkgRestore.Tests.ps1` (complete clones from a server that offers filtering; in-place
    completion; offline extraction as vcpkg performs it), the new `VcpkgToolCheckout.Tests.ps1`, and the
    `restore-dxui.ps1` wiring check in `DxUiRestoreTests.ps1`. Native compile: post-merge qualification.
