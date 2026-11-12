# Developer commands

Use the root build, test, validation and package entrypoints for ordinary qualification.

`Build-LocalSdkCandidate.ps1` explicitly compiles the Release application and its plugin
closure against a frozen local DxUi SDK. Restore the selected app dependencies with
`vcpkg-install.ps1` first. Output/intermediate paths use a fresh `.build/local-unpinned`
operation; the ordinary dependency tree stays separate. Every plugin checks the shared
live output lease before writes. Only the real DxUi consumers import its headers/archive
and ABI checks. The application test surface is unconditional, so the broker supplies no
invented test switch. The local build/link record grants no test, repository, package or
production qualification, and no application is launched, installed or published.

To exercise that candidate, use `test.ps1 -Configuration Release -SkipBuild -Suites
HostSmoke -LocalSdkBuildReceipt <original-record> -LocalSdkBuildSha256 <retained-digest>`.
This runs the existing hidden WARP, help/invalid-input and isolated crash checks
against the complete held candidate output. Native execution, current source and
engine, actual SDK ABI and every runtime byte must match; closing the reader is
required before a pass. Results stay in the separate local SDK evidence namespace
and cannot satisfy normal PrePush, main, packaging or production gates. Other
suites require their ordinary pinned build; this application-target recipe does
not build standalone test executables or the launcher.
