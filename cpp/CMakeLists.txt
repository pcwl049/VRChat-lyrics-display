cmake_minimum_required(VERSION 3.20)
project(MoeKoeVRChat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows 特定设置
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
    add_definitions(-DUNICODE -D_UNICODE)
    
    # C++/WinRT 支持
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /await")
endif()

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# 源文件
set(SOURCES
    src/main.cpp
    src/gsmtc_client.cpp
    src/moekoe_client.cpp
    src/lyrics_fetcher.cpp
    src/osc_sender.cpp
    src/config.cpp
    src/utils.cpp
)

# 头文件
set(HEADERS
    include/gsmtc_client.h
    include/moekoe_client.h
    include/lyrics_fetcher.h
    include/osc_sender.h
    include/config.h
    include/utils.h
    include/types.h
)

# 可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# 链接库
if(WIN32)
    target_link_libraries(${PROJECT_NAME}
        winhttp
        ws2_32
        ole32
        oleaut32
        uuid
        runtimeobject
        windowsapp
    )
endif()

# vcpkg 集成
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} nlohmann_json::nlohmann_json)

# 输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/Release"
)

# Release 优化
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GL /MD")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()

# 安装
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
