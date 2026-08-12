cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

project(test-sdmc)

# Install
# install directories
if(NOT CMAKE_INSTALL_PREFIX)
	message(FATAL_ERROR "ERROR: CMAKE_INSTALL_PREFIX is not defined.")
endif()
include(GNUInstallDirs)

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
	install(CODE "message(\"Installing SDMC test package\")")
endif() # CMAKE_INSTALL_FULL_LIBDIR
