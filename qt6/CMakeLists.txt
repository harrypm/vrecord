cmake_minimum_required(VERSION 3.21)
project(vrecord_qt6 LANGUAGES C CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

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
  ${DECKLINK_INCLUDE_DIR}/DeckLinkAPI_i.c
)

target_include_directories(VrecordQt6 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DECKLINK_INCLUDE_DIR}
)

target_link_libraries(VrecordQt6 PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  ole32
  oleaut32
  uuid
)

add_executable(vsignal
  src/SignalCli.cpp
  src/VrecordConfig.cpp
  src/DeckLinkDiscovery.cpp
  src/BackendController.cpp
  ${DECKLINK_INCLUDE_DIR}/DeckLinkAPI_i.c
)

target_include_directories(vsignal PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DECKLINK_INCLUDE_DIR}
)

target_link_libraries(vsignal PRIVATE
  Qt6::Core
  ole32
  oleaut32
  uuid
)

add_executable(VrecordOutputTest
  tests/OutputWiringTest.cpp
  src/VrecordConfig.cpp
  src/DeckLinkDiscovery.cpp
  src/BackendController.cpp
  ${DECKLINK_INCLUDE_DIR}/DeckLinkAPI_i.c
)

target_include_directories(VrecordOutputTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${DECKLINK_INCLUDE_DIR}
)

target_link_libraries(VrecordOutputTest PRIVATE
  Qt6::Core
  ole32
  oleaut32
  uuid
)
