// Product version shared by every version resource and by the packaging scripts.
//
// The build number is intentionally not edited here. build.ps1 -BuildNumber (the release workflow passes
// GITHUB_RUN_NUMBER) defines REDXE_VERSION_BUILD for the resource compiler; a plain local build stamps 0.
// Build/Versioning.psm1 reads the two human-maintained lines below with a regular expression, so keep each
// `#define` on one line with a single integer literal.
//
// This header is included from .rc files: the resource compiler does not reliably honor `#pragma once`.
#ifndef REDXE_COMMON_VERSION_H
#define REDXE_COMMON_VERSION_H

#define REDXE_VERSION_MAJOR 1
#define REDXE_VERSION_MINOR 0

#ifndef REDXE_VERSION_BUILD
#define REDXE_VERSION_BUILD 0
#endif

#define REDXE_VERSION_STRINGIZE_IMPL(value) #value
#define REDXE_VERSION_STRINGIZE(value) REDXE_VERSION_STRINGIZE_IMPL(value)

// 1,0,183,0 for FILEVERSION / PRODUCTVERSION and "1.0.183.0" for the string table.
#define REDXE_VERSION_NUMERIC REDXE_VERSION_MAJOR, REDXE_VERSION_MINOR, REDXE_VERSION_BUILD, 0
#define REDXE_VERSION_STRING                                                                                     \
    REDXE_VERSION_STRINGIZE(REDXE_VERSION_MAJOR)                                                                 \
    "." REDXE_VERSION_STRINGIZE(REDXE_VERSION_MINOR) "." REDXE_VERSION_STRINGIZE(REDXE_VERSION_BUILD) ".0"

#define REDXE_VERSION_COMPANY "RedSalamanders"
#define REDXE_VERSION_PRODUCT "RedXe"
#define REDXE_VERSION_COPYRIGHT "Copyright (c) 2026 RedSalamanders"

#endif
