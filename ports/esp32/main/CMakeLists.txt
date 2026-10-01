# The application is the ESP32-C6 port's; this board only brings its link.
set(C6 "${CMAKE_CURRENT_LIST_DIR}/../../esp32c6/main")
# The board configuration built into the firmware (pins and other hardware
# settings; see the Board configuration page of the documentation): board.conf next to this project
# if it exists (your own wiring; it is not in git), else board.example.conf.
# /fs/etc/board.conf on the device overrides it at run time.
set(TD_BOARD_CONF "${CMAKE_CURRENT_LIST_DIR}/../board.conf")
option(TD_USE_EXAMPLE_BOARD "Build generic firmware without private board.conf" OFF)
if(TD_USE_EXAMPLE_BOARD OR NOT EXISTS "${TD_BOARD_CONF}")
    set(TD_BOARD_CONF "${CMAKE_CURRENT_LIST_DIR}/../board.example.conf")
endif()
configure_file("${TD_BOARD_CONF}" "${CMAKE_CURRENT_BINARY_DIR}/board_builtin.conf" COPYONLY)

idf_component_register(
    SRCS
        "link_uart.c"
        "${C6}/main.c"
        "${C6}/hal_mux.c"
        "${C6}/tdsh_bridge_esp.c"
        "${C6}/net_esp.c"
        "${C6}/telnet.c"
        "${C6}/ota_esp.c"
        "${C6}/rs485.c"
        "../../common/td_proto_cmds.c"
    EMBED_TXTFILES "${CMAKE_CURRENT_BINARY_DIR}/board_builtin.conf"
    INCLUDE_DIRS "." "${C6}"
    LDFRAGMENTS "extram.lf"
    REQUIRES
        tinydesk
        tdsh
        nvs_flash
        esp_driver_uart
        app_update
        esp_https_ota
        json
        esp_http_client
        bootloader_support
        mbedtls
        esp_timer
        esp_netif
        esp_event
        lwip
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
message(STATUS "tinydesk board configuration: ${TD_BOARD_CONF}")
