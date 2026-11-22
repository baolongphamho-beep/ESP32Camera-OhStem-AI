set(MODEL_FILE "${CMAKE_CURRENT_LIST_DIR}/../models/yolo26n_224_s8_s3.espdl")
if(NOT EXISTS "${MODEL_FILE}")
    message(FATAL_ERROR "Missing quantized model: run tools/quantize.py before building. A .pt file cannot be embedded as an ESP-DL model.")
endif()
idf_component_register(SRCS "app_main.cpp" "edge_io.cpp" "servo_ctrl.cpp"
    INCLUDE_DIRS "." "../../../lib/Measurements"
    REQUIRES yolo26 esp_wifi esp_event esp_netif nvs_flash lwip esp_coex
             esp_driver_i2c esp_driver_ledc esp_driver_gpio esp_driver_usb_serial_jtag
             bt esp_timer esp_rom esp_pm esp_hw_support)
target_add_binary_data(${COMPONENT_LIB} "${MODEL_FILE}" BINARY)
set(SMARTSCALE_STANDBY_SCAN_PERCENT "100" CACHE STRING "Standby BLE scan duty: 10, 25, 50, 100")
if(NOT SMARTSCALE_STANDBY_SCAN_PERCENT MATCHES "^(10|25|50|100)$")
    message(FATAL_ERROR "SMARTSCALE_STANDBY_SCAN_PERCENT must be 10, 25, 50 or 100")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MEASUREMENT_CAMERA_TTL_MS=10000
    MEASUREMENT_HOLD_MS=10000
    EDGE_STANDBY_SCAN_PERCENT=${SMARTSCALE_STANDBY_SCAN_PERCENT})
if(SMARTSCALE_SCALE_ONLY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EDGE_SCALE_ONLY=1)
endif()
if(SMARTSCALE_CAMERA_CONTINUOUS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EDGE_CAMERA_CONTINUOUS=1)
endif()
if(SMARTSCALE_SERVO_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EDGE_SERVO_TEST=1)
endif()
