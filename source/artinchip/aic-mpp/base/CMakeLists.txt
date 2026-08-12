
add_library(mpp_base SHARED
    memory/dma_allocator.c
    memory/mpp_mem.c
    ringbuf/mpp_ringbuf.c
)

if (LWRB)
    target_link_libraries(mpp_base lwrb)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLWRB")
endif()

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
    install(TARGETS mpp_base RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR
