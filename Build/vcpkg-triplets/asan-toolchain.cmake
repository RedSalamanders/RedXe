# Runs from .build/vcpkg-triplets/<platform>-asan, beside the generated overlay.
include("${CMAKE_CURRENT_LIST_DIR}/../../vcpkg-tool/scripts/toolchains/windows.cmake")

# MSVC rejects /RTC1 together with /fsanitize=address.
foreach(lang C CXX)
    string(REPLACE "/RTC1" "" debug_flags "${CMAKE_${lang}_FLAGS_DEBUG}")
    set(CMAKE_${lang}_FLAGS_DEBUG "${debug_flags}" CACHE STRING "" FORCE)
endforeach()
unset(debug_flags)
