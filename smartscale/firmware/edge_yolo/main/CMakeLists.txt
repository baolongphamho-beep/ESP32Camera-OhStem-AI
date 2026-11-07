set(MODEL_FILE "${CMAKE_CURRENT_LIST_DIR}/../models/yolo26n_224_s8_s3.espdl")
if(NOT EXISTS "${MODEL_FILE}")
    message(FATAL_ERROR "Missing quantized model: run tools/quantize.py before building. A .pt file cannot be embedded as an ESP-DL model.")
endif()
idf_component_register(SRCS "app_main.cpp" "edge_io.cpp"
    INCLUDE_DIRS "." "../../../lib/Measurements"
    REQUIRES yolo26 esp_wifi esp_event esp_netif nvs_flash esp_http_client
             esp_driver_i2c esp_driver_usb_serial_jtag bt esp_timer)
target_add_binary_data(${COMPONENT_LIB} "${MODEL_FILE}" BINARY)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MEASUREMENT_CAMERA_TTL_MS=10000)
