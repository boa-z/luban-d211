cmake_minimum_required(VERSION 3.0 FATAL_ERROR)

# include_directories(include)

# Install
# install directories
add_executable(http_wificonfig http.c)
target_link_libraries(http_wificonfig wifimanager wpa_client)
if(NOT CMAKE_INSTALL_PREFIX)
    message(FATAL_ERROR "ERROR: CMAKE_INSTALL_PREFIX is not defined.")
endif()
include(GNUInstallDirs)

install(TARGETS http_wificonfig DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY html DESTINATION ${TARGET_DIR}/var/http_wificonfig)

install(FILES config/hostapd.conf DESTINATION ${TARGET_DIR}/etc/http_wificonfig)
install(FILES config/udhcpd.conf DESTINATION ${TARGET_DIR}/etc/http_wificonfig)