cmake_minimum_required(VERSION 3.21)

project(spray_control LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

option(SPRAY_ENABLE_DUCO "Build DUCO SDK controller adapter." OFF)
option(SPRAY_ENABLE_MODBUS "Build Qt SerialBus Modbus TCP client transport." OFF)
set(SPRAY_DUCO_INCLUDE_DIR "" CACHE PATH "DUCO SDK include directory")
set(SPRAY_DUCO_LIBRARY "" CACHE FILEPATH "DUCO SDK library")

find_package(Qt6 REQUIRED COMPONENTS Core Network Test Widgets Gui)
if(SPRAY_ENABLE_MODBUS)
    find_package(Qt6 REQUIRED COMPONENTS SerialBus)
endif()

enable_testing()

function(spray_copy_runtime_files target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/config"
            "$<TARGET_FILE_DIR:${target_name}>/config"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target_name}>/logs"
        VERBATIM)
endfunction()

set(SPRAY_UI_SOURCES
    src/ui/ApplicationHmiRunner.cpp
    src/ui/MainWindow.cpp
    src/ui/SnapshotProvider.cpp
    src/ui/HmiCommandPort.cpp
    src/ui/components/DeviceCardWidget.cpp
    src/ui/components/FlowMonitorWidget.cpp
    src/ui/models/QueueTableModel.cpp
    src/ui/models/EventLogTableModel.cpp
    src/ui/pages/OverviewPage.cpp
)
qt_add_resources(UI_RESOURCES resources/ui.qrc)
add_library(SprayHmiShell STATIC
    ${SPRAY_UI_SOURCES}
    ${UI_RESOURCES}
)
target_include_directories(SprayHmiShell PRIVATE src src/ui)
target_link_libraries(SprayHmiShell PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Network)


set(SPRAY_COMMON_SOURCES
    src/app/Application.cpp
    src/config/AppConfig.cpp
    src/domain/Payloads.cpp
    src/core/DequeueCoordinator.cpp
    src/core/EnqueueWorkflow.cpp
    src/core/LegacyCameraWorkflow.cpp
    src/core/QueueManager.cpp
    src/diagnostics/DeviceHealthRegistry.cpp
    src/diagnostics/DeviceSimulator.cpp
    src/diagnostics/DiagnosticsService.cpp
    src/diagnostics/EventLog.cpp
    src/diagnostics/RawFrameFileSink.cpp
    src/io/camera/CameraEndpoint.cpp
    src/io/plc/ModbusAddressTable.cpp
    src/io/plc/PlcModbusClient.cpp
    src/io/plc/PlcEndpoint.cpp
    src/protocol/ByteCodec.cpp
    src/protocol/CameraProtocol.cpp
    src/protocol/PlcProtocol.cpp
    src/robot/MotionPlanner.cpp
    src/robot/MotionRecipeTable.cpp
    src/robot/duco/DucoClientFactory.cpp
    src/robot/duco/DucoClient.cpp
    src/robot/duco/DucoMotionWorker.cpp
    src/robot/duco/DucoRobotController.cpp
    src/robot/FakeRobotController.cpp
    src/robot/IRobotController.cpp
)

if(SPRAY_ENABLE_DUCO)
    if(NOT SPRAY_DUCO_INCLUDE_DIR OR NOT SPRAY_DUCO_LIBRARY)
        message(FATAL_ERROR "SPRAY_ENABLE_DUCO requires SPRAY_DUCO_INCLUDE_DIR and SPRAY_DUCO_LIBRARY")
    endif()
endif()

qt_add_executable(spray_control
    src/app/main.cpp
    ${SPRAY_COMMON_SOURCES}
)

target_include_directories(spray_control PRIVATE src)
target_link_libraries(spray_control PRIVATE Qt6::Core Qt6::Network Qt6::Widgets Qt6::Gui SprayHmiShell)
target_compile_definitions(spray_control PRIVATE
    SPRAY_ENABLE_DUCO=$<BOOL:${SPRAY_ENABLE_DUCO}>
    SPRAY_ENABLE_MODBUS=$<BOOL:${SPRAY_ENABLE_MODBUS}>)
if(SPRAY_ENABLE_MODBUS)
    target_link_libraries(spray_control PRIVATE Qt6::SerialBus)
endif()
if(SPRAY_ENABLE_DUCO)
    target_include_directories(spray_control PRIVATE ${SPRAY_DUCO_INCLUDE_DIR})
    target_link_libraries(spray_control PRIVATE ${SPRAY_DUCO_LIBRARY})
endif()
set_target_properties(spray_control PROPERTIES WIN32_EXECUTABLE FALSE)
spray_copy_runtime_files(spray_control)

qt_add_executable(spray_hmi
    src/app/hmi_main.cpp
    ${SPRAY_COMMON_SOURCES}
)

target_include_directories(spray_hmi PRIVATE src)
target_link_libraries(spray_hmi PRIVATE Qt6::Core Qt6::Network Qt6::Widgets Qt6::Gui SprayHmiShell)
target_compile_definitions(spray_hmi PRIVATE
    SPRAY_ENABLE_DUCO=$<BOOL:${SPRAY_ENABLE_DUCO}>
    SPRAY_ENABLE_MODBUS=$<BOOL:${SPRAY_ENABLE_MODBUS}>)
if(SPRAY_ENABLE_MODBUS)
    target_link_libraries(spray_hmi PRIVATE Qt6::SerialBus)
endif()
if(SPRAY_ENABLE_DUCO)
    target_include_directories(spray_hmi PRIVATE ${SPRAY_DUCO_INCLUDE_DIR})
    target_link_libraries(spray_hmi PRIVATE ${SPRAY_DUCO_LIBRARY})
endif()
set_target_properties(spray_hmi PROPERTIES WIN32_EXECUTABLE FALSE)
spray_copy_runtime_files(spray_hmi)

qt_add_executable(spray_tests
    tests/test_main.cpp
    tests/test_app_config.cpp
    tests/test_application_loop.cpp
    tests/test_camera_endpoint.cpp
    tests/test_camera_protocol.cpp
    tests/test_diagnostics_event_log.cpp
    tests/test_diagnostics_service.cpp
    tests/test_device_simulator.cpp
    tests/test_duco_robot_controller.cpp
    tests/test_enqueue_workflow.cpp
    tests/test_fake_robot_loop.cpp
    tests/test_legacy_camera_workflow.cpp
    tests/test_modbus_address_table.cpp
    tests/test_motion_planner.cpp
    tests/test_motion_recipe_table.cpp
    tests/test_plc_endpoint.cpp
    tests/test_plc_modbus_client.cpp
    tests/test_plc_protocol.cpp
    tests/test_queue_manager.cpp
    ${SPRAY_COMMON_SOURCES}
)

target_include_directories(spray_tests PRIVATE src)
target_link_libraries(spray_tests PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_compile_definitions(spray_tests PRIVATE
    SPRAY_ENABLE_DUCO=$<BOOL:${SPRAY_ENABLE_DUCO}>
    SPRAY_ENABLE_MODBUS=$<BOOL:${SPRAY_ENABLE_MODBUS}>)
if(SPRAY_ENABLE_MODBUS)
    target_link_libraries(spray_tests PRIVATE Qt6::SerialBus)
endif()
if(SPRAY_ENABLE_DUCO)
    target_include_directories(spray_tests PRIVATE ${SPRAY_DUCO_INCLUDE_DIR})
    target_link_libraries(spray_tests PRIVATE ${SPRAY_DUCO_LIBRARY})
endif()
set_target_properties(spray_tests PROPERTIES WIN32_EXECUTABLE FALSE)
spray_copy_runtime_files(spray_tests)

add_test(NAME spray_tests COMMAND spray_tests)
