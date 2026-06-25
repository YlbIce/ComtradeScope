cmake_minimum_required(VERSION 3.24)

if (POLICY CMP0167)
  cmake_policy(SET CMP0167 OLD)
endif()

project(comtradescope_native_backend LANGUAGES CXX)

if (DEFINED CMAKE_TOOLCHAIN_FILE)
  message(STATUS "Using toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

if (MSVC)
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
  set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS beast system)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(comtradescope-backend
  src/main.cpp
  src/ComtradeAnalyzer.cpp
  src/ComtradeParser.cpp
  src/ProtocolUtils.cpp
  src/WebSocketServer.cpp
)

target_compile_definitions(comtradescope-backend PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WIN32_WINNT=0x0A00
)

target_include_directories(comtradescope-backend PRIVATE src)

if (MSVC)
  target_compile_options(comtradescope-backend PRIVATE /utf-8 /permissive-)
endif()

target_link_libraries(comtradescope-backend PRIVATE
  Boost::beast
  Boost::system
  nlohmann_json::nlohmann_json
  ws2_32
)
