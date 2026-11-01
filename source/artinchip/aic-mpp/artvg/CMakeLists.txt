
add_library(artvg SHARED
    artvg.c
    artvg_matrix.c
    artvg_image.c
    artvg_path.c
    drv/linux_drv.c
    os/linux_os.c
)

set_target_properties(artvg PROPERTIES FOLDER "artvg")

target_include_directories(artvg PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(artvg mpp_base)

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS artvg LIBRARY DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR
