cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# Force standard drag-and-drop folder structure
set(OBS_PLUGIN_DESTINATION "obs-plugins/64bit")
set(OBS_PLUGIN_DATA_DESTINATION "data/obs-plugins/${CMAKE_PROJECT_NAME}")

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)

# --- NEW: Fetch IXWebSocket Library ---
include(FetchContent)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.4
)
set(IXWEBSOCKET_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_USE_TLS OFF CACHE BOOL "" FORCE)

# --- FIX: Tell the compiler to relax and not treat warnings as errors ---
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
if(MSVC)
    add_compile_options(/WX-)
endif()
# ------------------------------------------------------------------------

FetchContent_MakeAvailable(ixwebsocket)
# --------------------------------------
# --- NEW: Fetch FFmpeg for H.264 decoding ---
   set(FFMPEG_ZIP "ffmpeg-master-latest-win64-gpl-shared.zip")
   set(FFMPEG_URL "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/${FFMPEG_ZIP}")
   set(FFMPEG_DIR "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg")

   if(NOT EXISTS "${FFMPEG_DIR}")
       message(STATUS "Downloading FFmpeg Dev Libraries...")
       file(DOWNLOAD "${FFMPEG_URL}" "${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_ZIP}")
       execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf "${CMAKE_CURRENT_BINARY_DIR}/${FFMPEG_ZIP}" WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
       file(RENAME "${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-master-latest-win64-gpl-shared" "${FFMPEG_DIR}")
   endif()

   target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${FFMPEG_DIR}/include")
   target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE "${FFMPEG_DIR}/lib")
   target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE avcodec avutil)

   # Bundle the FFmpeg DLLs into our final zip file so OBS can use them!
   file(GLOB FFMPEG_DLLS "${FFMPEG_DIR}/bin/*.dll")
   install(FILES ${FFMPEG_DLLS} DESTINATION bin/64bit)
   # ----------------------------------------------
# --- NEW: Connect the Zoom Meeting SDK ---
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/zoom-sdk/h")
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/zoom-sdk/lib")
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE sdk)
# -----------------------------------------   
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs ixwebsocket ws2_32 crypt32)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-main.cpp)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
# --- FIX: Force the DLL into the correct drag-and-drop folder ---
install(TARGETS ${CMAKE_PROJECT_NAME}
    LIBRARY DESTINATION ${OBS_PLUGIN_DESTINATION}
    RUNTIME DESTINATION ${OBS_PLUGIN_DESTINATION}
)
# --- FIX: Move the .pdb debug file right next to the .dll ---
if(MSVC)
    install(FILES $<TARGET_PDB_FILE:${CMAKE_PROJECT_NAME}>
        DESTINATION ${OBS_PLUGIN_DESTINATION}
        OPTIONAL
    )
endif()
