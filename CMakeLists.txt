cmake_minimum_required(VERSION 3.21)

project(spray_control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Test)

enable_testing()

set(SPRAY_COMMON_SOURCES
    src/app/Application.cpp
    src/config/AppConfig.cpp
    src/domain/Payloads.cpp
    src/core/DequeueCoordinator.cpp
    src/core/QueueManager.cpp
    src/diagnostics/EventLog.cpp
    src/io/plc/PlcEndpoint.cpp
    src/protocol/ByteCodec.cpp
    src/protocol/CameraProtocol.cpp
    src/protocol/PlcProtocol.cpp
    src/robot/FakeRobotController.cpp
)

qt_add_executable(spray_control
    src/app/main.cpp
    ${SPRAY_COMMON_SOURCES}
)

target_include_directories(spray_control PRIVATE src)
target_link_libraries(spray_control PRIVATE Qt6::Core Qt6::Network)
set_target_properties(spray_control PROPERTIES WIN32_EXECUTABLE FALSE)

qt_add_executable(spray_tests
    tests/test_main.cpp
    tests/test_application_loop.cpp
    tests/test_camera_protocol.cpp
    tests/test_fake_robot_loop.cpp
    tests/test_plc_endpoint.cpp
    tests/test_plc_protocol.cpp
    tests/test_queue_manager.cpp
    ${SPRAY_COMMON_SOURCES}
)

target_include_directories(spray_tests PRIVATE src)
target_link_libraries(spray_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
set_target_properties(spray_tests PROPERTIES WIN32_EXECUTABLE FALSE)

add_test(NAME spray_tests COMMAND spray_tests)
