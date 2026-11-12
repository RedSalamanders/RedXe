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
