cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

project(aic_widgets LANGUAGES C)

# Suppress cmake unused warning
set(ignore ${BUILD_DOC}    ${BUILD_DOCS} ${BUILD_EXAMPLE} ${BUILD_EXAMPLES}
    ${BUILD_SHARED_LIBS}${BUILD_TEST}${BUILD_TESTING}${BUILD_TESTS})

add_compile_options(-Wall -Werror)

# set include path
set(INCLUDE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../${APP_FOLDER}
)

# Set source file
file(GLOB WIDGETS_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

file(GLOB AIC_CANVAS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/aic_canvas/*.c)

if(DEFINED LVGL_V_9)
    if(${LVGL_V_9} STREQUAL "yes")
        file(GLOB AIC_CANVAS_FILES_V9 ${CMAKE_CURRENT_SOURCE_DIR}/aic_canvas/v9/*.c)
        file(GLOB AIC_PLAYER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/v9/*.c)
        list(APPEND AIC_CANVAS_FILES ${AIC_CANVAS_FILES_V9})
        list(APPEND INCLUDE_PATH
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_canvas/v9
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v9
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v9/lvgl
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v9/lv_drivers/fbdev/lv_mpp_dec
        )
        list(APPEND PUBLIC_INCLUDE_PATH
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_canvas/v9
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/v9
        )
        if(BUILD_LYRICS_LIBS)
            ### build lyrics_effect library
            add_subdirectory(lyrics_effect)
        endif()
    else()
        list(APPEND INCLUDE_PATH
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v8
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v8/lvgl
        )
    endif()
endif()

set(WIDGETS_SOURCE_FILES ${WIDGETS_SOURCE_FILES} ${AIC_CANVAS_FILES} ${AIC_PLAYER_FILES})
add_library(aic_widgets STATIC ${WIDGETS_SOURCE_FILES})

# add include path
target_include_directories(aic_widgets PRIVATE ${INCLUDE_PATH})
target_include_directories(aic_widgets PUBLIC ${PUBLIC_INCLUDE_PATH})

# Install
if(NOT CMAKE_INSTALL_PREFIX)
    message(FATAL_ERROR "ERROR: CMAKE_INSTALL_PREFIX is not defined.")
endif()
include(GNUInstallDirs)
