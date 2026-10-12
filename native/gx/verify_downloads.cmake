# Aurora fetches a few archives by tag or release name without a hash. For iOS
# builds (GalaxyPad's New engine) check them against recorded SHA-256 values, so a
# changed upstream archive fails the build instead of changing the engine. Update
# these entries together with the Aurora pin in native/gx/CMakeLists.txt.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_petari_pinned_downloads
        "sdl|release-3.4.10.tar.gz|0dc11d980ba17250200718fa4e28011da293f27ed92f92203afffe396811f307"
        "abseil-cpp|20240722.0.tar.gz|f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3"
        "dawn_prebuilt|dawn-ios-arm64.tar.gz|64e70875040099e24db3a40095461d43be0c553f0c347d63628f6d469c7d184b")
    if(NOT FETCHCONTENT_BASE_DIR)
        set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")
    endif()
    foreach(_entry IN LISTS _petari_pinned_downloads)
        string(REPLACE "|" ";" _parts "${_entry}")
        list(GET _parts 0 _name)
        list(GET _parts 1 _file)
        list(GET _parts 2 _expected)
        file(GLOB_RECURSE _archives "${FETCHCONTENT_BASE_DIR}/${_name}-subbuild/${_file}")
        if(NOT _archives)
            message(FATAL_ERROR "Pinned download ${_file} (${_name}) was not found under ${FETCHCONTENT_BASE_DIR}; "
                "if Aurora's dependency versions changed, update native/gx/verify_downloads.cmake")
        endif()
        foreach(_archive IN LISTS _archives)
            file(SHA256 "${_archive}" _actual)
            if(NOT _actual STREQUAL _expected)
                message(FATAL_ERROR "${_archive}: SHA-256 ${_actual}, expected ${_expected}")
            endif()
        endforeach()
    endforeach()
    message(STATUS "Pinned downloads verified: SDL, abseil, Dawn (iOS)")
endif()
