cmake_minimum_required(VERSION 3.20)
project(NovaRenderer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 选项
option(NOVA_BUILD_EXAMPLES "Build example applications" ON)
option(NOVA_ENABLE_VALIDATION "Enable Vulkan validation layers" ON)
option(NOVA_ENABLE_VSYNC "Enable VSync by default" ON)

# 设置 Vulkan SDK 路径
if(DEFINED ENV{VULKAN_SDK})
    set(VULKAN_SDK_PATH $ENV{VULKAN_SDK})
else()
    set(VULKAN_SDK_PATH "E:/Vulkan_SDK")
endif()

# 查找 Vulkan
find_package(Vulkan REQUIRED)

# 手动设置 glslang 库路径
set(GLSLANG_LIBRARIES
    "${VULKAN_SDK_PATH}/Lib/glslang.lib"
    "${VULKAN_SDK_PATH}/Lib/SPIRV.lib"
    "${VULKAN_SDK_PATH}/Lib/SPIRV-Tools-opt.lib"
    "${VULKAN_SDK_PATH}/Lib/SPIRV-Tools.lib"
    "${VULKAN_SDK_PATH}/Lib/shaderc_combined.lib"
)
set(GLSLANG_INCLUDE_DIR "${VULKAN_SDK_PATH}/Include")

# VMA (Vulkan Memory Allocator) - 单头文件库
add_library(vma INTERFACE)
target_include_directories(vma INTERFACE 
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma/include"
    "${VULKAN_SDK_PATH}/Include"
)

# STB 库 - 单头文件
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")

# SPIRV-Reflect - 使用本地源代码
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv-reflect/CMakeLists.txt")
    set(SPIRV_REFLECT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/spirv-reflect")
else()
    # 创建一个简单的 spirv-reflect 库
    add_library(spirv-reflect-static INTERFACE)
    target_include_directories(spirv-reflect-static INTERFACE "${VULKAN_SDK_PATH}/Include")
endif()

# FreeType
find_package(Freetype QUIET)
if(NOT Freetype_FOUND)
    # 如果没有 FreeType，创建一个空目标
    message(STATUS "FreeType not found, font rendering will be limited")
    add_library(nova_freetype INTERFACE)
else()
    add_library(nova_freetype INTERFACE)
    target_link_libraries(nova_freetype INTERFACE Freetype::Freetype)
endif()

# 源文件
set(NOVA_SOURCES
    src/Window.cpp
    src/Input.cpp
    src/VulkanBackend.cpp
    src/Renderer.cpp
    src/Shader.cpp
    src/Pipeline.cpp
    src/Descriptor.cpp
    src/Command.cpp
    src/Texture.cpp
    src/Font.cpp
    src/ThreadPool.cpp
    src/RenderGraph.cpp
    src/ParticleSystem.cpp
    src/PostProcess.cpp
    src/Profiler.cpp
    src/AsyncCompute.cpp
    src/Instancing.cpp
    src/TextureStreaming.cpp
    src/ShaderHotReload.cpp
    src/Material.cpp
    src/Scene.cpp
    src/LiquidGlass.cpp
)

set(NOVA_HEADERS
    include/Nova/Types.h
    include/Nova/Window.h
    include/Nova/Input.h
    include/Nova/VulkanBackend.h
    include/Nova/Renderer.h
    include/Nova/Shader.h
    include/Nova/Pipeline.h
    include/Nova/Descriptor.h
    include/Nova/Command.h
    include/Nova/Texture.h
    include/Nova/Font.h
    include/Nova/ThreadPool.h
    include/Nova/RenderGraph.h
    include/Nova/ParticleSystem.h
    include/Nova/PostProcess.h
    include/Nova/Profiler.h
    include/Nova/AsyncCompute.h
    include/Nova/Instancing.h
    include/Nova/TextureStreaming.h
    include/Nova/ShaderHotReload.h
    include/Nova/Material.h
    include/Nova/Scene.h
    include/Nova/LiquidGlass.h
)

# 创建静态库
add_library(NovaCore STATIC ${NOVA_SOURCES} ${NOVA_HEADERS})

target_include_directories(NovaCore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${Vulkan_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma/include"
        ${GLSLANG_INCLUDE_DIR}
)

target_link_libraries(NovaCore
    PUBLIC
        Vulkan::Vulkan
        nova_freetype
    PRIVATE
        vma
        stb
        ${GLSLANG_LIBRARIES}
)

target_compile_definitions(NovaCore
    PRIVATE
        $<$<BOOL:${NOVA_ENABLE_VALIDATION}>:NOVA_ENABLE_VALIDATION>
        $<$<BOOL:${NOVA_ENABLE_VSYNC}>:NOVA_ENABLE_VSYNC>
)

# Windows 特定设置
if(WIN32)
    target_compile_definitions(NovaCore PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        VK_USE_PLATFORM_WIN32_KHR
    )
    target_link_libraries(NovaCore PRIVATE user32 gdi32)
endif()

# 编译选项
if(MSVC)
    target_compile_options(NovaCore PRIVATE
        /W4
        /WX-  # 警告不作为错误
        /utf-8
        $<$<CONFIG:Release>:/O2 /GL>
        $<$<CONFIG:Debug>:/Od /Zi>
    )
    target_link_options(NovaCore PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
else()
    target_compile_options(NovaCore PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g -O0>
    )
endif()

# 示例应用
if(NOVA_BUILD_EXAMPLES)
    add_executable(NovaExample examples/main.cpp)
    target_link_libraries(NovaExample PRIVATE NovaCore)
    
    if(MSVC)
        target_compile_options(NovaExample PRIVATE /utf-8)
    endif()
    
    if(WIN32)
        set_target_properties(NovaExample PROPERTIES
            WIN32_EXECUTABLE FALSE
            LINK_FLAGS "/SUBSYSTEM:CONSOLE"
        )
    endif()
    
    # Liquid Glass Example
    add_executable(LiquidGlass examples/liquid_glass.cpp)
    target_link_libraries(LiquidGlass PRIVATE NovaCore)
    if(WIN32)
        set_target_properties(LiquidGlass PROPERTIES
            WIN32_EXECUTABLE FALSE
            LINK_FLAGS "/SUBSYSTEM:CONSOLE"
        )
    endif()
endif()

# 打印配置信息
message(STATUS "")
message(STATUS "Nova Renderer Configuration:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Vulkan SDK:     ${VULKAN_SDK_PATH}")
message(STATUS "  Validation:     ${NOVA_ENABLE_VALIDATION}")
message(STATUS "  VSync:          ${NOVA_ENABLE_VSYNC}")
message(STATUS "  Build Examples: ${NOVA_BUILD_EXAMPLES}")
message(STATUS "")