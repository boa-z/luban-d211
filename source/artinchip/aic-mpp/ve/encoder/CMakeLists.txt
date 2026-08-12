set(ENCODER_JPEG
    jpeg/jpeg_tables.c
    jpeg/jpeg_enc.c
    jpeg/jpeg_enc_hal.c
)

if (H264_ENCODER)
set(ENCODER_H264
    h264/h264_encoder.c
    h264/h264_enc_hal.c
    h264/h264_rc.c
    h264/h264_quality_metrics.c
)
endif()

set(ENCODER_SRC
    mpp_encoder.c
    enc_frame_manager.c
    enc_packet_manager.c
    ${ENCODER_JPEG}
    ${ENCODER_H264}
)

add_library(mpp_encoder SHARED
    ${ENCODER_SRC}
)

target_link_libraries(mpp_encoder mpp_base mpp_ve m)

target_include_directories(mpp_encoder PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/jpeg"
    "${CMAKE_CURRENT_SOURCE_DIR}/h264"
)

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS mpp_encoder RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR

if (H264_ENCODER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DH264_ENCODER_ENABLE")
endif()
