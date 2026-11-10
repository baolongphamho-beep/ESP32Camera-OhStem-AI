cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../../.edge-deps/esp-dl/models/yolo26")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smartscale_edge_yolo)
