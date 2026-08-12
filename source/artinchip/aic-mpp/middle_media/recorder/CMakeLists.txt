add_library(media_recorder SHARED
	aic_recorder.c
)
target_link_libraries(media_recorder mpp_base mm_component)

target_link_libraries(media_recorder pthread)

target_include_directories(media_recorder PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../base/include"
)

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS media_recorder RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR


include_directories(./include ../component/include ../base/include ../audio_decoder/include ../../base/include)

add_executable(recorder_demo recorder_demo.c test/recorder_cam.c)

target_link_libraries(recorder_demo mpp_base mm_base mm_component media_recorder)

target_link_libraries(recorder_demo pthread)

set_target_properties(recorder_demo PROPERTIES INSTALL_RPATH "/usr/local/lib")

#install(TARGETS recorder_demo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(GNUInstallDirs)

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS recorder_demo RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_BINDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR

if(DEFINED CMAKE_INSTALL_FULL_DATAROOTDIR)
    install(DIRECTORY ../base/osd/osd_font
            DESTINATION ${CMAKE_INSTALL_FULL_DATAROOTDIR}/)
endif() # CMAKE_INSTALL_FULL_DATAROOTDIR


