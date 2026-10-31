# ThorVG SVG rendering

This project contains SVG widgets, so it was exported with **ThorVG mode** on.
SVG assets are emitted as vector data for LVGL's runtime SVG decoder instead of being
rasterized into bitmaps, so the build needs the LVGL vector stack enabled and compiled.

## What the exporter already did

- Enabled LV_USE_FLOAT, LV_USE_MATRIX, LV_USE_VECTOR_GRAPHIC, LV_USE_THORVG_INTERNAL, LV_USE_SVG in `main/lv_conf.h`.
- Enabled CONFIG_LV_USE_FLOAT, CONFIG_LV_USE_MATRIX, CONFIG_LV_USE_VECTOR_GRAPHIC, CONFIG_LV_USE_THORVG, CONFIG_LV_USE_THORVG_INTERNAL, CONFIG_LV_USE_SVG in `components/espressif__esp_lcd_st77916/test_apps/sdkconfig.defaults`.
- Enabled CONFIG_LV_USE_FLOAT, CONFIG_LV_USE_MATRIX, CONFIG_LV_USE_VECTOR_GRAPHIC, CONFIG_LV_USE_THORVG, CONFIG_LV_USE_THORVG_INTERNAL, CONFIG_LV_USE_SVG in `components/espressif__esp_lcd_touch_cst816s/test_apps/sdkconfig.defaults`.
- Enabled CONFIG_LV_USE_FLOAT, CONFIG_LV_USE_MATRIX, CONFIG_LV_USE_VECTOR_GRAPHIC, CONFIG_LV_USE_THORVG, CONFIG_LV_USE_THORVG_INTERNAL, CONFIG_LV_USE_SVG in `sdkconfig`.
- Enabled CONFIG_LV_USE_FLOAT, CONFIG_LV_USE_MATRIX, CONFIG_LV_USE_VECTOR_GRAPHIC, CONFIG_LV_USE_THORVG, CONFIG_LV_USE_THORVG_INTERNAL, CONFIG_LV_USE_SVG in `sdkconfig.defaults`.

## What you still need to do

- Nothing — just build the project.

## Reference

LVGL config options (lv_conf.h, or the matching `CONFIG_…` symbols with ESP-IDF Kconfig):

- `LV_USE_FLOAT`
- `LV_USE_MATRIX`
- `LV_USE_VECTOR_GRAPHIC`
- `LV_USE_THORVG_INTERNAL`
- `LV_USE_SVG`

With ESP-IDF Kconfig, `LV_USE_THORVG` must be enabled as well — it is the parent of the
internal/external ThorVG choice and has no lv_conf.h counterpart.

Sources the build must compile (they ship inside the LVGL tree):

- `lvgl/src/libs/thorvg/*.cpp` — C++14 or newer, and a non-trivial heap (ThorVG uses `new`)
- `lvgl/src/libs/svg/*.c`

IDEs with a file-by-file source list (Keil µVision, IAR EW) need those files added to the
LVGL group manually; recursive/glob-based builds (CMake, ESP-IDF, Arduino, Eclipse CDT
managed build) pick them up on their own.
