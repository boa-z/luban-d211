set(BASE_MSG
    message/aic_message.c
    comm/aic_tag.c
    comm/audio_frame_manager.c
    comm/audio_packet_manager.c
)

if (HTTP_STREAM)
set(STREAM_HTTP
    stream/http/aic_http_stream.c
)
endif()

if (RTP_STREAM)
    set(STREAM_RTP
        stream/rtp/aic_rtp_stream.c
        stream/rtp/rtp_packet.c
    )
    if (RTP_SORTER_ENABLE)
        list(APPEND STREAM_RTP stream/rtp/rtp_sorter.c)
    endif()
endif()

set(BASE_STREAM
    stream/aic_stream.c
    stream/file/aic_file_stream.c
    ${STREAM_HTTP}
    ${STREAM_RTP}
)

if (WAV_DEMUXER)
set(DEMUXER_WAV
    parser/wav/aic_wav_parser.c
    parser/wav/wav.c
)
endif()

if (AVI_DEMUXER)
set(DEMUXER_AVI
    parser/avi/aic_avi_parser.c
    parser/avi/avi.c
)
endif()

if (MKV_DEMUXER)
set(DEMUXER_MKV
    parser/mkv/aic_mkv_parser.c
    parser/mkv/matroska.c
    parser/mkv/matroska_tag.c
)
endif()

if (TS_DEMUXER)
set(DEMUXER_TS
    parser/ts/aic_ts_parser.c
    parser/ts/mpegts.c
    parser/ts/mpegts_audio.c
)
endif()

if (FLV_DEMUXER)
set(DEMUXER_FLV
    parser/flv/aic_flv_parser.c
    parser/flv/flv.c
)
endif()

if (RTSP_DEMUXER)
set(DEMUXER_RTSP
    parser/rtsp/aic_rtsp_parser.cpp
    parser/rtsp/rtsp.cpp
)
endif()

if (HLS_DEMUXER)
set(DEMUXER_HLS
    parser/hls/aic_hls_parser.c
    parser/hls/m3u.c
)
endif()

if (OGG_DEMUXER)
set(DEMUXER_OGG
    parser/ogg/aic_ogg_parser.c
    parser/ogg/ogg_parser.c
)
endif()

if (AAC_DECODER)
set(DEMUXER_AAC
    parser/aac/aic_aac_parser.c
)
endif()

if (APE_DECODER)
set(DEMUXER_APE
    parser/ape/aic_ape_parser.c
)
endif()

if (FLAC_DECODER)
set(DEMUXER_FLAC
    parser/flac/aic_flac_parser.c
    parser/flac/flac.c
)
endif()

if (MPG_DEMUXER)
set(DEMUXER_MPG
    parser/mpg/aic_mpg_parser.c
    parser/mpg/mpg.c
)
endif()

set(BASE_PARSER
    parser/aic_parser.c
    parser/mov/aic_mov_parser.c
    parser/mov/mov_tags.c
    parser/mov/mov.c
    parser/rawdata/aic_raw_parser.c
    parser/mp3/aic_mp3_parser.c
    parser/mp3/mp3.c
    ${DEMUXER_WAV}
    ${DEMUXER_AVI}
    ${DEMUXER_MKV}
    ${DEMUXER_TS}
    ${DEMUXER_FLV}
    ${DEMUXER_HLS}
    ${DEMUXER_OGG}
    ${DEMUXER_AAC}
    ${DEMUXER_APE}
    ${DEMUXER_FLAC}
    ${DEMUXER_MPG}
)

if (MP4_MUXER)
set(MUXER_MP4
    muxer/mp4/aic_mp4_muxer.c
    muxer/mp4/mp4_muxer.c
)
endif()

if (TS_MUXER)
set(MUXER_TS
    muxer/ts/aic_ts_muxer.c
    muxer/ts/ts_muxer.c
)
endif()

set(BASE_MUXER
    muxer/aic_muxer.c
    ${MUXER_MP4}
    ${MUXER_TS}
)

if (RECORDER)
    set(BASE_OSD
        osd/aic_osd.c
    )
    set(BASE_STORAGE
        storage/aic_storage.c
    )
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRECORDER")
endif()

if (RENDER_DRM_BACKEND)
set(BASE_RENDER
    render/aic_video_drm_render.c
)
else()
set(BASE_RENDER
    render/aic_video_fbdev_render.c
)
endif()

set(BASE_AUDIO_RENDER
    render/aic_audio_render.c
)

add_library(mm_base SHARED
    ${BASE_MSG}
    ${BASE_STREAM}
    ${BASE_PARSER}
    ${BASE_MUXER}
    ${BASE_OSD}
    ${BASE_STORAGE}
    ${BASE_RENDER}
    ${BASE_AUDIO_RENDER}
)

target_link_libraries(mm_base mpp_base mpp_ge asound)

if (RENDER_DRM_BACKEND)
    target_link_libraries(mm_base drm)
endif()

if (RTSP_DEMUXER)
    target_link_libraries(mm_base BasicUsageEnvironment groupsock liveMedia UsageEnvironment)
endif()

if (AAC_DECODER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAAC_DECODER")
endif()

if (APE_DECODER)
    target_link_libraries(mm_base ape)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPE_DECODER")
endif()

if (FLAC_DECODER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DFLAC_DECODER")
endif()

if (VORBIS_DECODER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVORBIS_DECODER")
endif()

if (WAV_DEMUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DWAV_DEMUXER")
endif()

if (AVI_DEMUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAVI_DEMUXER")
endif()

if (MKV_DEMUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMKV_DEMUXER")
endif()

if (TS_DEMUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DTS_DEMUXER")
endif()

if (FLV_DEMUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DFLV_DEMUXER")
endif()

if (RTSP_DEMUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRTSP_DEMUXER")
endif()

if (HLS_DEMUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHLS_DEMUXER")
endif()

if (OGG_DEMUXER)
    target_link_libraries(mm_base ogg)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DOGG_DEMUXER")
endif()

if (MP4_MUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMP4_MUXER")
endif()

if (TS_MUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DTS_MUXER")
endif()

if (MPG_DEMUXER)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMPG_DEMUXER")
endif()

if (HTTP_STREAM)
    target_link_libraries(mm_base curl)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHTTP_STREAM")
endif()

if (RTP_STREAM)
    target_link_libraries(mm_base)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRTP_STREAM")
endif()
if (SQLITE)
    target_link_libraries(mm_base sqlite3)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DSQLITE")
endif()
if (RTP_SORTER_ENABLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRTP_SORTER_ENABLE")
endif()

target_include_directories(mm_base PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/comm"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/mov"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/avi"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/mkv"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/flv"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/rawdata"
    "${CMAKE_CURRENT_SOURCE_DIR}/stream/file"
    "${CMAKE_CURRENT_SOURCE_DIR}/stream/http"
    "${CMAKE_CURRENT_SOURCE_DIR}/stream/rtp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../audio_decoder/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/mp3"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/wav"
    "${CMAKE_CURRENT_SOURCE_DIR}/muxer/mp4"
    "${CMAKE_CURRENT_SOURCE_DIR}/muxer/ts"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/rtsp"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/hls"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/ogg"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/aac"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/ape"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/flac"
    "${CMAKE_CURRENT_SOURCE_DIR}/parser/mpg"
)

if (RENDER_DRM_BACKEND)
target_include_directories(mm_base PUBLIC
    "/usr/include/drm"
)
endif()

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS mm_base RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR

