cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

project(wifi_ap_config LANGUAGES C)

# Suppress cmake unused warning
set(ignore ${BUILD_DOC}	${BUILD_DOCS} ${BUILD_EXAMPLE} ${BUILD_EXAMPLES}
	${BUILD_SHARED_LIBS}${BUILD_TEST}${BUILD_TESTING}${BUILD_TESTS})

add_compile_options(-Wall -Werror)

add_executable(wifi_ap_config wifi_ap_config.c)

#target_link_libraries(p2p_auto wpa_client)
target_link_libraries(wifi_ap_config wifimanager wpa_client)

# Install
# install directories
if(NOT CMAKE_INSTALL_PREFIX)
	message(FATAL_ERROR "ERROR: CMAKE_INSTALL_PREFIX is not defined.")
endif()
include(GNUInstallDirs)

install(FILES files/udhcpd.conf DESTINATION ${TARGET_DIR}/etc/wifi)
install(FILES files/hostapd.conf DESTINATION ${TARGET_DIR}/etc/wifi)

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
	install(TARGETS wifi_ap_config RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_BINDIR}")
endif() # CMAKE_INSTALL_FULL_LIBDIR
