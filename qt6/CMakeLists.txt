cmake_minimum_required(VERSION 3.21)
project(vrecord_qt6 LANGUAGES C CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

# one version for everything: the VERSION line of the vrecord shell script
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../vrecord" VRECORD_VERSION_LINE REGEX "^VERSION=\"")
string(REGEX REPLACE "^VERSION=\"([^\"]*)\".*" "\\1" VRECORD_VERSION "${VRECORD_VERSION_LINE}")
if(NOT VRECORD_VERSION)
  set(VRECORD_VERSION "0")
endif()
# GitHub repository whose published releases the updater offers
set(VRECORD_UPDATE_REPO "harrypm/vrecord" CACHE STRING "owner/name of the GitHub repository with vrecord releases")
message(STATUS "vrecord version ${VRECORD_VERSION}, updates from ${VRECORD_UPDATE_REPO}")

# ---------------------------------------------------------------- DeckLink SDK
# Windows: the SDK's IDL output (DeckLinkAPI.h + DeckLinkAPI_i.c) from the
#          CI's decklink-include folder (VRECORD_BUILD, default ~/vrecord-build)
# macOS / Linux: the MediaArea header mirror (Release_Mac.sh clones it to
#          Release/decklink-sdk-headers; Homebrew's decklinksdk formula also
#          works) - its DeckLinkAPIDispatch.cpp provides the entry points
if(WIN32)
  set(VRECORD_BUILD_DIR "$ENV{VRECORD_BUILD}")
  if(NOT VRECORD_BUILD_DIR)
    set(VRECORD_BUILD_DIR "$ENV{USERPROFILE}/vrecord-build")
  endif()
  set(DECKLINK_INCLUDE_DIR "${VRECORD_BUILD_DIR}/decklink-include" CACHE PATH "Path to DeckLink SDK headers")
  set(DECKLINK_SOURCES "${DECKLINK_INCLUDE_DIR}/DeckLinkAPI_i.c")
  set(DECKLINK_LIBS ole32 oleaut32 uuid)
else()
  if(APPLE)
    set(_decklink_platform Mac)
  else()
    set(_decklink_platform Linux)
  endif()
  find_path(DECKLINK_INCLUDE_DIR DeckLinkAPI.h
    HINTS
      "$ENV{DECKLINK_SDK}"
      "${CMAKE_CURRENT_SOURCE_DIR}/../Release/decklink-sdk-headers/DeckLinkSDK/${_decklink_platform}/include"
      "/opt/homebrew/opt/decklinksdk/include"
      "/usr/local/opt/decklinksdk/include"
      "/home/linuxbrew/.linuxbrew/opt/decklinksdk/include"
    DOC "Path to DeckLink SDK headers (the folder with DeckLinkAPI.h and DeckLinkAPIDispatch.cpp)")
  if(NOT DECKLINK_INCLUDE_DIR)
    message(FATAL_ERROR "DeckLink SDK headers not found. Set DECKLINK_SDK, or clone "
                        "https://github.com/MediaArea/decklink-sdk-headers to Release/decklink-sdk-headers.")
  endif()
  set(DECKLINK_SOURCES "${DECKLINK_INCLUDE_DIR}/DeckLinkAPIDispatch.cpp")
  if(APPLE)
    set(DECKLINK_LIBS "-framework CoreFoundation")
  else()
    set(DECKLINK_LIBS dl pthread)
  endif()
endif()
message(STATUS "DeckLink SDK headers: ${DECKLINK_INCLUDE_DIR}")

# the DeckLink dispatch unit is shared by the three programs
add_library(decklink_dispatch STATIC ${DECKLINK_SOURCES})
target_include_directories(decklink_dispatch PUBLIC ${DECKLINK_INCLUDE_DIR})
target_link_libraries(decklink_dispatch PUBLIC ${DECKLINK_LIBS})
if(NOT WIN32)
  # the SDK's dispatch file is not warning-clean under -Wall
  target_compile_options(decklink_dispatch PRIVATE -w)
endif()

# ---------------------------------------------------------------- common code
set(VRECORD_BACKEND_SOURCES
  src/VrecordConfig.cpp
  src/DeckLinkDiscovery.cpp
  src/BackendController.cpp
  src/FrameFeed.cpp
  src/PipeFeed.cpp
  src/Platform.cpp
)

set(VRECORD_APP_RESOURCES resources/vrecord.qrc)
if(WIN32)
  list(APPEND VRECORD_APP_RESOURCES resources/vrecord.rc)
endif()
if(APPLE)
  set(VRECORD_MAC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/vrecord.icns")
  set_source_files_properties("${VRECORD_MAC_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  list(APPEND VRECORD_APP_RESOURCES "${VRECORD_MAC_ICON}")
endif()

# ---------------------------------------------------------------- the GUI
add_executable(VrecordQt6 WIN32 MACOSX_BUNDLE
  src/main.cpp
  src/MainWindow.cpp
  src/VideoView.cpp
  src/LevelMeter.cpp
  src/ComponentStatus.cpp
  src/DeckControl.cpp
  src/UpdateManager.cpp
  ${VRECORD_BACKEND_SOURCES}
  ${VRECORD_APP_RESOURCES}
)

target_include_directories(VrecordQt6 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(VrecordQt6 PRIVATE
  VRECORD_VERSION="${VRECORD_VERSION}"
  VRECORD_UPDATE_REPO="${VRECORD_UPDATE_REPO}"
)

target_link_libraries(VrecordQt6 PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Network
  decklink_dispatch
)

if(APPLE)
  set_target_properties(VrecordQt6 PROPERTIES
    OUTPUT_NAME "VrecordQt6"
    MACOSX_BUNDLE_BUNDLE_NAME "vrecord"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.harrypm.vrecord.qt6"
    MACOSX_BUNDLE_BUNDLE_VERSION "${VRECORD_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${VRECORD_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "vrecord.icns"
  )
endif()

# ---------------------------------------------------------------- CLI tools
add_executable(vsignal
  src/SignalCli.cpp
  ${VRECORD_BACKEND_SOURCES}
)
if(WIN32)
  target_sources(vsignal PRIVATE resources/vrecord.rc)
endif()
target_include_directories(vsignal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vsignal PRIVATE Qt6::Core Qt6::Gui decklink_dispatch)

add_executable(VrecordOutputTest
  tests/OutputWiringTest.cpp
  ${VRECORD_BACKEND_SOURCES}
)
target_include_directories(VrecordOutputTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VrecordOutputTest PRIVATE Qt6::Core Qt6::Gui decklink_dispatch)

# ---------------------------------------------------------------- install
include(GNUInstallDirs)
if(APPLE)
  install(TARGETS VrecordQt6 BUNDLE DESTINATION .)
elseif(NOT WIN32)
  install(TARGETS VrecordQt6 vsignal RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
