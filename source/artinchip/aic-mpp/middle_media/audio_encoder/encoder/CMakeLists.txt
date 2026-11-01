if (MP3_ENCODER)
set(ENCODER_MP3
    mp3/mp3_encoder.c
)
endif()

if (AAC_ENCODER)
set(ENCODER_AAC
    aac/aac_encoder.c
)
endif()

add_library(mpp_audio_encoder SHARED
    ${ENCODER_MP3}
    ${ENCODER_AAC}
    aic_audio_encoder.c
)

target_include_directories(mpp_audio_encoder PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(mpp_audio_encoder mm_base mpp_base)
if (MP3_ENCODER)
    target_link_libraries(mpp_audio_encoder mp3lame)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMP3_ENCODER")
endif()
if (AAC_ENCODER)
    target_link_libraries(mpp_audio_encoder fdk-aac)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DAAC_ENCODER")
endif()

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS mpp_audio_encoder RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR
