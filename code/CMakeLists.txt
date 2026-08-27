cmake_minimum_required(VERSION 3.16)
project(helm_x VERSION 0.0.10 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(
        $<$<CONFIG:Release>:-Os>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )
    add_link_options(
        $<$<CONFIG:Release>:-static>
        $<$<CONFIG:Release>:-static-libgcc>
        $<$<CONFIG:Release>:-static-libstdc++>
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Release>:-Wl,--strip-all>
    )
endif()

add_executable(helmx
    src/main.cpp
    src/config.cpp
    src/log.cpp
    src/resources.cpp
    src/resources_generated.cpp
    src/tamper.cpp
    src/proxy.cpp
    src/sse.cpp
    src/rewrite.cpp
    src/http.cpp
    src/ui.cpp
    src/verify.cpp
    src/watch.cpp
    src/icon.rc
)

target_include_directories(helmx PRIVATE src)

if(WIN32)
    target_link_libraries(helmx ws2_32 shell32 winhttp)
endif()
