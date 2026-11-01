
include_directories(../include ../base/include ../../base/include)

add_executable(audio_encoder_test wavreader.c audio_encoder_test.c )

target_link_libraries(audio_encoder_test mpp_base mm_base mpp_audio_encoder)

target_link_libraries(audio_encoder_test pthread)

set_target_properties(audio_encoder_test PROPERTIES INSTALL_RPATH "/usr/local/lib")

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS audio_encoder_test RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_BINDIR}")

endif() # CMAKE_INSTALL_FULL_LIBDIR
