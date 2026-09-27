# tinydesk firmware for the classic ESP32 (tested on an ESP32-WROVER-IE:
# 16 MB flash, 8 MB PSRAM), ESP-IDF 5.3.1.
#
#   idf.py build
#   idf.py -p COMx flash        (close PuTTY first)
#
# The desktop is served on UART0 (the board's USB-UART chip) at 921600 baud;
# the ESP-IDF console is off (logs go to the Log Viewer). The application
# code is shared with the ESP32-C6 port (../esp32c6/main); only the link and
# the board facts differ (main/link_uart.c).
cmake_minimum_required(VERSION 3.16)

get_filename_component(TD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(TDSH_SDK_ROOT "${TD_ROOT}/third_party/tdsh")

set(IDF_TARGET "esp32" CACHE STRING "tinydesk target")
list(APPEND EXTRA_COMPONENT_DIRS
    "${TDSH_SDK_ROOT}/ports/esp_idf/components"
    "${TD_ROOT}/ports/esp32c6/components")

# TinyDesk Shell's pinned third-party components (littlefs, wolfSSL, W6100, libsmb2).
# The ESP32-C6 project already has them in managed_components; use those
# (with IDF_COMPONENT_MANAGER=0) so this port builds offline. Without them,
# leave the component manager on and it downloads them.
if(EXISTS "${TD_ROOT}/ports/esp32c6/managed_components/wolfssl__wolfssl")
    list(APPEND EXTRA_COMPONENT_DIRS "${TD_ROOT}/ports/esp32c6/managed_components")
endif()

if(DEFINED ENV{TD_VERSION})
    set(PROJECT_VER "$ENV{TD_VERSION}")
else()
    set(PROJECT_VER "0.1.0")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tinydesk)

include("${TDSH_SDK_ROOT}/ports/esp_idf/tdsh_espidf_project_setup.cmake")

# With the component manager off, dependencies that only its manifests
# declare must be linked here: the W6100 driver needs wiznet_common.
idf_component_get_property(TD_W6100_LIB espressif__w6100 COMPONENT_LIB)
idf_component_get_property(TD_WIZNET_LIB espressif__wiznet_common COMPONENT_LIB)
if(TD_W6100_LIB AND TD_WIZNET_LIB)
    target_link_libraries(${TD_W6100_LIB} PUBLIC ${TD_WIZNET_LIB})
endif()
