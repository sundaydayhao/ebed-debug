cmake_minimum_required(VERSION 3.21)

project(ebed-debug-pro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt6 COMPONENTS SerialPort QUIET)
find_package(Qt6 COMPONENTS Charts QUIET)
find_package(Qt6 COMPONENTS Mqtt QUIET)

add_executable(ebed-debug-pro
  src/main.cpp
  src/Domain.h
  src/MainWindow.h
  src/MainWindow.cpp
  src/ProtocolUtils.h
  src/ProtocolUtils.cpp
  src/TemplateRepository.h
  src/TemplateRepository.cpp
  src/TransportClient.h
  src/TransportClient.cpp
)

target_link_libraries(ebed-debug-pro PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Network
)

if(TARGET Qt6::SerialPort)
  message(STATUS "Qt6 SerialPort found: serial and Modbus RTU support enabled")
  target_link_libraries(ebed-debug-pro PRIVATE Qt6::SerialPort)
  target_compile_definitions(ebed-debug-pro PRIVATE EBED_HAS_QT_SERIALPORT=1)
else()
  message(WARNING "Qt6 SerialPort not found. Serial and Modbus RTU support will be disabled. Install qt6-serialport-dev, then re-run CMake.")
endif()

if(TARGET Qt6::Charts)
  message(STATUS "Qt6 Charts found: chart view enabled")
  target_link_libraries(ebed-debug-pro PRIVATE Qt6::Charts)
  target_compile_definitions(ebed-debug-pro PRIVATE EBED_HAS_QT_CHARTS=1)
else()
  message(STATUS "Qt6 Charts not found: chart page will fall back to text summary")
endif()

if(TARGET Qt6::Mqtt)
  message(STATUS "Qt6 Mqtt found: MQTT support enabled")
  target_link_libraries(ebed-debug-pro PRIVATE Qt6::Mqtt)
  target_compile_definitions(ebed-debug-pro PRIVATE EBED_HAS_QT_MQTT=1)
else()
  message(WARNING "Qt6 Mqtt not found. MQTT support will be disabled. Install qt6-mqtt-dev, then re-run CMake.")
endif()
