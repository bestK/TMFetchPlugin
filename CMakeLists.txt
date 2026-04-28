cmake_minimum_required(VERSION 3.16)
project(TMFetchPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "TMFetchPlugin only builds on Windows (TrafficMonitor target).")
endif()

# Build a 32-bit DLL by default to match TrafficMonitor.exe (which is shipped as x86).
# Configure with: cmake -A Win32 -B build
# For x64 TrafficMonitor: cmake -A x64 -B build64

# nlohmann/json (single header) - downloaded once on first configure
set(JSON_INCLUDE_DIR ${CMAKE_BINARY_DIR}/_deps/json)
set(JSON_HPP ${JSON_INCLUDE_DIR}/nlohmann/json.hpp)
if(NOT EXISTS ${JSON_HPP})
    file(MAKE_DIRECTORY ${JSON_INCLUDE_DIR}/nlohmann)
    message(STATUS "Downloading nlohmann/json single header...")
    file(DOWNLOAD
        https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
        ${JSON_HPP}
        SHOW_PROGRESS
        STATUS _json_dl_status
    )
    list(GET _json_dl_status 0 _json_dl_code)
    if(NOT _json_dl_code EQUAL 0)
        message(FATAL_ERROR "Failed to download nlohmann/json: ${_json_dl_status}")
    endif()
endif()

enable_language(RC)

add_library(TMFetchPlugin SHARED
    src/dllmain.cpp
    src/HttpFetchPlugin.cpp
    src/HttpFetchItem.cpp
    src/HttpClient.cpp
    src/JsonPath.cpp
    src/Config.cpp
    src/StringUtil.cpp
    src/OptionsDialog.cpp
    src/Dialogs.rc
    src/TMFetchPlugin.def
)

target_include_directories(TMFetchPlugin PRIVATE
    include
    ${JSON_INCLUDE_DIR}
)

target_compile_definitions(TMFetchPlugin PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(TMFetchPlugin PRIVATE
    winhttp
    shlwapi
    comctl32
)

if(MSVC)
    target_compile_options(TMFetchPlugin PRIVATE /W3 /utf-8 /EHsc)
endif()

# Print the bitness so a mismatch with TrafficMonitor.exe is obvious at configure time.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(STATUS "TMFetchPlugin: building 32-bit (x86) DLL  -- matches official TrafficMonitor.exe")
else()
    message(WARNING "TMFetchPlugin: building 64-bit DLL. Official TrafficMonitor.exe is 32-bit; "
                    "loading this DLL will fail with '不是有效的 Win32 程序'. "
                    "Use a 32-bit toolchain (MSVC -A Win32, or i686-mingw).")
endif()

set_target_properties(TMFetchPlugin PROPERTIES
    OUTPUT_NAME "TMFetchPlugin"
    PREFIX ""
    SUFFIX ".dll"
)
