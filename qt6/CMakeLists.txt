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

set(VRECORD_BUILD_DIR "$ENV{VRECORD_BUILD}")
if(NOT VRECORD_BUILD_DIR)
  set(VRECORD_BUILD_DIR "$ENV{USERPROFILE}/vrecord-build")
endif()

set(DECKLINK_INCLUDE_DIR "${VRECORD_BUILD_DIR}/decklink-include" CACHE PATH "Path to DeckLink SDK headers")

add_executable(VrecordQt6 WIN32
  src/main.cpp
  src/MainWindow.cpp
  src/VrecordConfig.cpp
  src/DeckLinkDiscovery.cpp
  src/BackendController.cpp
  src/FrameFeed.cpp
  src/VideoView.cpp
  src/LevelMeter.cpp
  src/PipeFeed.cpp
  src/ComponentStatus.cpp
  src/DeckControl.cpp
  src/UpdateManager.cpp
  resources/vrecord.rc
  resources/vrecord.qrc
  ${DECKLINK_INCLUDE_DIR}/DeckLinkAPI_i.c
)

target_include_directories(VrecordQt6 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DECKLINK_INCLUDE_DIR}
)

target_compile_definitions(VrecordQt6 PRIVATE
  VRECORD_VERSION="${VRECORD_VERSION}"
  VRECORD_UPDATE_REPO="${VRECORD_UPDATE_REPO}"
)

target_link_libraries(VrecordQt6 PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Network
  ole32
  oleaut32
  uuid
)

add_executable(vsignal
  src/SignalCli.cpp
  resources/vrecord.rc
  src/VrecordConfig.cpp
  src/DeckLinkDiscovery.cpp
  src/BackendController.cpp
  src/FrameFeed.cpp
  src/PipeFeed.cpp
  ${DECKLINK_INCLUDE_DIR}/DeckLinkAPI_i.c
)

target_include_directories(vsignal PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DECKLINK_INCLUDE_DIR}
)

target_link_libraries(vsignal PRIVATE
  Qt6::Core
  Qt6::Gui
  ole32
  oleaut32
  uuid
)

add_executable(VrecordOutputTest
  tests/OutputWiringTest.cpp
  src/VrecordConfig.cpp
  src/DeckLinkDiscovery.cpp
  src/BackendController.cpp
  src/FrameFeed.cpp
  src/PipeFeed.cpp
  ${DECKLINK_INCLUDE_DIR}/DeckLinkAPI_i.c
)

target_include_directories(VrecordOutputTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DECKLINK_INCLUDE_DIR}
)

target_link_libraries(VrecordOutputTest PRIVATE
  Qt6::Core
  Qt6::Gui
  ole32
  oleaut32
  uuid
)
