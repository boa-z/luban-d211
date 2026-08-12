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
        file(GLOB AIC_PLAYER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/*.c
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend/player_backend_ops.c
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend/player_backend_map.c
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend/backend_common.c
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend/aic_backend_ops.c)

        file(GLOB AIC_CANVAS_FILES_V9 ${CMAKE_CURRENT_SOURCE_DIR}/aic_canvas/v9/*.c)
        file(GLOB AIC_CAMERA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/aic_camera/v9/*.c)
        if(DEFINED USE_RECORDER)
            if(${USE_RECORDER} STREQUAL "yes")
                file(GLOB AIC_RECORDER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/aic_recorder/v9/*.c)
            endif()
        endif()
        file(GLOB AIC_VIDEO_WINDOW_FILES ${CMAKE_CURRENT_SOURCE_DIR}/aic_video_window/*.c)
        list(APPEND AIC_CANVAS_FILES ${AIC_CANVAS_FILES_V9})
        list(APPEND INCLUDE_PATH
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_canvas/v9
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v9
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v9/lvgl
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v9/lv_drivers/fbdev/lv_mpp_dec
        )
        list(APPEND PUBLIC_INCLUDE_PATH
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_video_window
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_canvas/v9
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_camera/v9
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player
            if(DEFINED USE_RECORDER)
                if(${USE_RECORDER} STREQUAL "yes")
                    ${CMAKE_CURRENT_SOURCE_DIR}/aic_recorder/v9
                endif()
            endif()
        )
        if(BUILD_LYRICS_LIBS)
            ### build lyrics_effect library
            add_subdirectory(lyrics_effect)
        endif()
    else()
        file(GLOB AIC_PLAYER_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/*.c
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend/player_backend_ops.c
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend/player_backend_map.c
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend/backend_common.c
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend/aic_backend_ops.c)
        file(GLOB AIC_VIDEO_WINDOW_FILES ${CMAKE_CURRENT_SOURCE_DIR}/aic_video_window/*.c)
        list(APPEND INCLUDE_PATH
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v8
            ${CMAKE_CURRENT_SOURCE_DIR}/../lvgl_v8/lvgl
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player/player_backend
        )
        list(APPEND PUBLIC_INCLUDE_PATH
            ${CMAKE_CURRENT_SOURCE_DIR}/aic_player
        )
    endif()
endif()

set(WIDGETS_SOURCE_FILES ${WIDGETS_SOURCE_FILES} ${AIC_CANVAS_FILES} ${AIC_PLAYER_FILES} ${AIC_VIDEO_WINDOW_FILES})
if(AICAST_WIDGETS)
    set(WIDGETS_SOURCE_FILES ${WIDGETS_SOURCE_FILES} ${AICAST_FILES})
endif()
if(DEFINED USE_RECORDER)
    if(${USE_RECORDER} STREQUAL "yes")
        set(WIDGETS_SOURCE_FILES ${WIDGETS_SOURCE_FILES} ${AIC_CANVAS_FILES} ${AIC_PLAYER_FILES} ${AIC_CAMERA_FILES} ${AIC_RECORDER_FILES})
    endif()
endif()

add_library(aic_widgets STATIC ${WIDGETS_SOURCE_FILES})

# add include path
target_include_directories(aic_widgets PRIVATE ${INCLUDE_PATH})
target_include_directories(aic_widgets PUBLIC ${PUBLIC_INCLUDE_PATH})
if(AICAST_WIDGETS)
    target_link_libraries(aic_widgets aicast aicastbase)
endif()

# Install
if(NOT CMAKE_INSTALL_PREFIX)
    message(FATAL_ERROR "ERROR: CMAKE_INSTALL_PREFIX is not defined.")
endif()
include(GNUInstallDirs)
