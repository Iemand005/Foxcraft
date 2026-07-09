cmake_minimum_required(VERSION 3.15)
project(Tunnel)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(KISSFFT_TEST OFF CACHE BOOL "Disable KissFFT tests" FORCE)

set(KISSFFT_STATIC ON CACHE BOOL "Build static library" FORCE)

# set(FE_EXCLUDE_JOLT OFF CACHE BOOL "Disable JOLT" FORCE)

set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../FenixEngine")

# add_subdirectory(${CMAKE_SOURCE_DIR}/../FenixEngine/src/Engine ${CMAKE_BINARY_DIR}/out/Engine)

add_subdirectory(${ENGINE_DIR}/external/JoltPhysics/Build ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/jolt)

if(NOT TARGET Engine)
    message(STATUS "Engine target not found. Looking for engine source...")
    
    if(EXISTS "${ENGINE_DIR}/CMakeLists.txt")
        add_subdirectory("${ENGINE_DIR}" FenixEngine)
    # elif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/FenixEngine/CMakeLists.txt")
    #     add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/FenixEngine" FenixEngine)
    else()
        message(FATAL_ERROR "FenixEngine not found!")
    endif()
endif()

set(IMGUI_DIR ../FenixEngine/external/imgui)

file(GLOB_RECURSE IMGUI_SOURCE_FILES
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
	${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp
	${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
	${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
	${IMGUI_DIR}/imgui_demo.cpp
	${IMGUI_DIR}/imgui_draw.cpp
)

add_executable(Foxcraft
	WIN32
	${IMGUI_SOURCE_FILES}
	main.cpp
)

target_include_directories(Foxcraft PRIVATE
	include
	${ENGINE_DIR}
	${ENGINE_DIR}/include
	${ENGINE_DIR}/external
	${ENGINE_DIR}/external/imgui
	${ENGINE_DIR}/external/kissfft
	${ENGINE_DIR}/external/SDL/include
	${ENGINE_DIR}/src/Engine
)

target_link_libraries(Foxcraft PRIVATE
	Engine
)

if(NOT WIN32)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
target_include_directories(Foxcraft PRIVATE ${PIPEWIRE_INCLUDE_DIRS})
target_link_libraries(Foxcraft PRIVATE ${PIPEWIRE_LIBRARIES})
target_compile_options(Foxcraft PRIVATE ${PIPEWIRE_CFLAGS_OTHER})

find_package(X11 REQUIRED)

target_link_libraries(Foxcraft PRIVATE X11::X11)
endif()

set_property(TARGET Foxcraft PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
	
if(MSVC)
	target_compile_options(Foxcraft PRIVATE $<$<CONFIG:Debug>:/Zi>)

	target_link_options(Foxcraft PRIVATE $<$<CONFIG:Debug>:/DEBUG>)

	add_compile_options(/RTC1)
endif()

set_property(TARGET Foxcraft PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
