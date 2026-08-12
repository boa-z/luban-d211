set(DECODER_MJPEG
    jpeg/jpeg_hal.c
    jpeg/mjpeg_decoder.c
)

set(DECODER_PNG
    png/png_decoder.c
    png/png_hal.c
)

set(DECODER_AVC
    h264/h264_decoder.c
    h264/h264_hal.c
    h264/h264_nal.c
    h264/h264_refs.c
)

if (MPEG12_DECODER)
set(DECODER_MPEG12
    mpeg12/mpeg12_decoder.c
    mpeg12/table.c
    mpeg12/mpeg12_register.c
    mpeg12/mpeg12_mv.c
)
endif()

if (MPEG4_DECODER)
set(DECODER_MPEG4
    mpeg4/mp4_block_311.c
    mpeg4/mp4_block.c
    mpeg4/mp4_decore.c
    mpeg4/mp4_decore_util.c
    mpeg4/mp4_dpart.c
    mpeg4/mp4_global.c
    mpeg4/mp4_header_311.c
    mpeg4/mp4_header.c
    mpeg4/mp4_init.c
    mpeg4/mp4_mblock_bvop.c
    mpeg4/mp4_mblock.c
    mpeg4/mp4_mblock_util.c
    mpeg4/mp4_picture_311.c
    mpeg4/mp4_picture.c
    mpeg4/mp4_predict_311.c
    mpeg4/mp4_predict.c
    mpeg4/mp4_recon_bvop.c
    mpeg4/mp4_recon.c
    mpeg4/mp4_recon_gmc.c
    mpeg4/mp4_tables.c
    mpeg4/mp4_vld_311.c
    mpeg4/mp4_vld.c
    mpeg4/mp4_vld_r.c
    mpeg4/mpeg4_decoder.c
    mpeg4/mpeg4_register.c
)
endif()

add_library(mpp_decoder SHARED
    ${DECODER_MJPEG}
    ${DECODER_PNG}
    ${DECODER_AVC}
    ${DECODER_MPEG12}
    ${DECODER_MPEG4}
    aicp/aicp_wrapper.c
    mpp_decoder.c
    frame_manager.c
    packet_manager.c
)

target_link_libraries(mpp_decoder mpp_base mpp_ve dl m)

target_include_directories(mpp_decoder PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/jpeg"
    "${CMAKE_CURRENT_SOURCE_DIR}/png"
    "${CMAKE_CURRENT_SOURCE_DIR}/h264"
)

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS mpp_decoder RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR

if (MPEG12_DECODER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMPEG12_DECODER")
endif()

if (MPEG4_DECODER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMPEG4_DECODER")
endif()
