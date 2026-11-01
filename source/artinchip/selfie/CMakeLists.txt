cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

project(selfie LANGUAGES C)

# Suppress cmake unused warning
set(ignore ${BUILD_DOC}	${BUILD_DOCS} ${BUILD_EXAMPLE} ${BUILD_EXAMPLES}
	${BUILD_SHARED_LIBS}${BUILD_TEST}${BUILD_TESTING}${BUILD_TESTS})

add_compile_options(-Wall -Werror)

add_executable(selfie selfie.c agent.c gatt.c adapter.c log.c advertise.c key.c)

target_include_directories(selfie PUBLIC ${STAGING_DIR}/usr/include/glib-2.0/)
target_include_directories(selfie PUBLIC ${STAGING_DIR}/usr/lib/glib-2.0/include/)
target_include_directories(selfie PUBLIC ${STAGING_DIR}/usr/include/dbus-1.0/)
target_include_directories(selfie PUBLIC ${STAGING_DIR}/usr/lib/dbus-1.0/include/)

find_library(LIBGLIB libglib-2.0.so)
find_library(LIBGIO libgio-2.0.so)
find_library(LIBGOBJECT libgobject-2.0.so)
find_library(LIBDBUS libdbus-1.so)

add_library(selfie_hid SHARED
	agent.c
	gatt.c
	adapter.c
	log.c
	advertise.c
)

target_include_directories(selfie_hid PUBLIC ${STAGING_DIR}/usr/include/glib-2.0/)
target_include_directories(selfie_hid PUBLIC ${STAGING_DIR}/usr/lib/glib-2.0/include/)
target_include_directories(selfie_hid PUBLIC ${STAGING_DIR}/usr/include/dbus-1.0/)
target_include_directories(selfie_hid PUBLIC ${STAGING_DIR}/usr/lib/dbus-1.0/include/)

target_link_libraries(selfie pthread ${LIBGLIB} ${LIBGIO} ${LIBGOBJECT} ${LIBDBUS})

# Install
# install directories
if(NOT CMAKE_INSTALL_PREFIX)
	message(FATAL_ERROR "ERROR: CMAKE_INSTALL_PREFIX is not defined.")
endif()
include(GNUInstallDirs)

if(DEFINED CMAKE_INSTALL_FULL_INCLUDEDIR)
    install(FILES log.h DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/")
    install(FILES gatt.h DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/")
    install(FILES agent.h DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/")
    install(FILES adapter.h DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/")
    install(FILES advertise.h DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/")
endif() # CMAKE_INSTALL_FULL_INCLUDEDIR
if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
	install(TARGETS selfie_hid RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR
if(DEFINED CMAKE_INSTALL_FULL_BINDIR)
	install(TARGETS selfie RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_BINDIR}")
endif() # CMAKE_INSTALL_FULL_BINDIR
