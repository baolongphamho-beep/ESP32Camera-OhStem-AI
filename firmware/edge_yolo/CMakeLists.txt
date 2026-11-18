cmake_minimum_required(VERSION 3.16)
foreach(_candidate
    "${CMAKE_CURRENT_LIST_DIR}/../../.edge-deps/esp-dl/models/yolo26"
    "${CMAKE_CURRENT_LIST_DIR}/../../../.edge-deps/esp-dl/models/yolo26"
    "${CMAKE_CURRENT_LIST_DIR}/../../../../.edge-deps/esp-dl/models/yolo26")
    if(EXISTS "${_candidate}/CMakeLists.txt")
        set(EXTRA_COMPONENT_DIRS "${_candidate}")
        break()
    endif()
endforeach()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smartscale_edge_yolo)
