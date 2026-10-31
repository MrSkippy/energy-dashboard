set(UI_DIR ./GUI)
# CONFIGURE_DEPENDS is rejected as a hard error in CMake's script (-P) mode,
# which the IDF requirements-expansion pass uses; only request it in build mode.
if(CMAKE_SCRIPT_MODE_FILE)
    file(GLOB_RECURSE UI_SOURCES "${UI_DIR}/*.c" "${UI_DIR}/*.cpp")
else()
    file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "${UI_DIR}/*.c" "${UI_DIR}/*.cpp")
endif()

idf_component_register(
    SRCS "main.cpp" "background_rotator.c" "knob.c" "home_clock.c" "price.c" "home_price.c"
         "settings_store.c" "wifi_manager.c" "setup_portal.c" "screen_setup.c" "screen_settings.c" "tibber.c"
         ${UI_SOURCES}
    INCLUDE_DIRS "." "${UI_DIR}"
    EMBED_TXTFILES "web/setup.html"
)
