set(DECODER_MP3
    mp3/mp3_decoder.c
)

if (AAC_DECODER)
set(DECODER_AAC
    aac/aac_decoder.c
)
endif()

if (VORBIS_DECODER)
set(DECODER_VORBIS
    vorbis/vorbis_decoder.c
)
endif()

if (OPUS_DECODER)
set(DECODER_OPUS
    opus/opus_decoder.c
)
endif()

if (AAC_ELD_DECODER)
set(DECODER_AAC_ELD
    aac_eld/aac_eld_decoder.c
)
endif()

if (ALAC_DECODER)
set(DECODER_ALAC
    alac/alac_decoder.c
)
endif()

if (APE_DECODER)
set(DECODER_APE
    ape/ape_decoder.c
)
endif()

if (FLAC_DECODER)
set(DECODER_FLAC
    flac/flac_decoder.c
)
endif()

add_library(mpp_audio_decoder SHARED
    ${DECODER_MP3}
    ${DECODER_AAC}
    ${DECODER_VORBIS}
    ${DECODER_OPUS}
    ${DECODER_AAC_ELD}
    ${DECODER_ALAC}
    ${DECODER_APE}
    ${DECODER_FLAC}
    aic_audio_decoder.c
)

target_include_directories(mpp_audio_decoder PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(mpp_audio_decoder mm_base mpp_base mad)
if (AAC_DECODER)
    target_link_libraries(mpp_audio_decoder faad)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAAC_DECODER")
endif()

if (VORBIS_DECODER)
    target_link_libraries(mpp_audio_decoder vorbis)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DVORBIS_DECODER")
endif()

if (OPUS_DECODER)
    target_link_libraries(mpp_audio_decoder opus)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DOPUS_DECODER")
endif()

if (AAC_ELD_DECODER)
    target_link_libraries(mpp_audio_decoder fdk-aac)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAAC_ELD_DECODER")
endif()

if (ALAC_DECODER)
    target_link_libraries(mpp_audio_decoder alac)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DALAC_DECODER")
endif()

if (APE_DECODER)
    target_link_libraries(mpp_audio_decoder ape)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAPE_DECODER")
endif()

if (FLAC_DECODER)
    target_link_libraries(mpp_audio_decoder FLAC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DFLAC_DECODER")
endif()

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS mpp_audio_decoder RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR
