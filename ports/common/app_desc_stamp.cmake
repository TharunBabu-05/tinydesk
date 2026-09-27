# app_desc_stamp.cmake - include after project(): recompile ESP-IDF's
# esp_app_desc.c on every build.
#
# The firmware's build date and time (About, Software Update, `ota status`)
# are that file's __DATE__/__TIME__. ESP-IDF compiles it only once per build
# directory, so an incremental build kept the date of the first one.
idf_component_get_property(TD_APP_DESC_DIR esp_app_format COMPONENT_DIR)
idf_component_get_property(TD_APP_DESC_LIB esp_app_format COMPONENT_LIB)
set(TD_APP_DESC_STAMP "${CMAKE_BINARY_DIR}/td_app_desc.stamp")
add_custom_target(td_app_desc_stamp
    COMMAND ${CMAKE_COMMAND} -E touch "${TD_APP_DESC_STAMP}"
    BYPRODUCTS "${TD_APP_DESC_STAMP}"
    VERBATIM)
add_dependencies(${TD_APP_DESC_LIB} td_app_desc_stamp)
set_source_files_properties("${TD_APP_DESC_DIR}/esp_app_desc.c"
    TARGET_DIRECTORY ${TD_APP_DESC_LIB}
    PROPERTIES OBJECT_DEPENDS "${TD_APP_DESC_STAMP}")
