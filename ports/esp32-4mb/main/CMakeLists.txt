# The classic ESP32 with 4 MB flash and no PSRAM: the shared application
# (ports/esp_idf/app) with the desktop on UART0 (link_uart.c); this project
# only brings the 4 MB / no-PSRAM configuration.
include("${CMAKE_CURRENT_LIST_DIR}/../../esp_idf/app/app.cmake")

idf_component_register(
    SRCS "${TD_ESP_APP_DIR}/link_uart.c" ${TD_ESP_APP_SRCS}
    EMBED_TXTFILES "${TD_BOARD_BUILTIN_CONF}"
    INCLUDE_DIRS "${TD_ESP_APP_DIR}"
    REQUIRES ${TD_ESP_APP_REQUIRES}
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
