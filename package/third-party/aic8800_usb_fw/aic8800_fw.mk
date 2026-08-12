AIC8800_USB_FW_SOURCE = aic8800_usb_fw.tar.gz
AIC8800_USB_FW_SITE = aic8800_usb_fw
AIC8800_USB_FW_ENABLE_TARBALL = YES
AIC8800_USB_FW_ENABLE_PATCH = NO
AIC8800_USB_FW_INSTALL_STAGING = YES
AIC8800_USB_FW_DEPENDENCIES = linux

AIC8800_USB_FW_CONF_OPTS += -DCMAKE_INSTALL_PREFIX=/etc/firmware

# USB firmware tarball files are flat — organize into chip subdirs and generate CMakeLists.txt.
define AIC8800_USB_FW_GENERATE_CMAKE
	$(Q)mkdir -p $(@D)/aic8800D80
	$(Q)mv $(@D)/*.txt $(@D)/*.bin $(@D)/aic8800D80/ 2>/dev/null || true
	$(Q)echo 'cmake_minimum_required(VERSION 3.0 FATAL_ERROR)' > $(@D)/CMakeLists.txt
	$(Q)echo 'project(aic8800_usb_fw LANGUAGES C)' >> $(@D)/CMakeLists.txt
	$(Q)echo 'include(GNUInstallDirs)' >> $(@D)/CMakeLists.txt
	$(Q)echo 'if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)' >> $(@D)/CMakeLists.txt
	$(Q)echo '	install(DIRECTORY DESTINATION "$${CMAKE_INSTALL_PREFIX}/")' >> $(@D)/CMakeLists.txt
	$(Q)echo 'if(AIC8800_USB_FW_ID_AIC8800D80)' >> $(@D)/CMakeLists.txt
	$(Q)echo '	file(GLOB FW_FILES "aic8800D80/*.txt" "aic8800D80/*.bin")' >> $(@D)/CMakeLists.txt
	$(Q)echo '	install(FILES $${FW_FILES} DESTINATION "$${CMAKE_INSTALL_PREFIX}/aic8800D80/")' >> $(@D)/CMakeLists.txt
	$(Q)echo 'endif()' >> $(@D)/CMakeLists.txt
	$(Q)echo 'endif()' >> $(@D)/CMakeLists.txt
endef
AIC8800_USB_FW_POST_EXTRACT_HOOKS += AIC8800_USB_FW_GENERATE_CMAKE

ifeq ($(AIC8800_USB_FW_ID_AIC8800),y)
    AIC8800_USB_FW_CONF_OPTS += -DAIC8800_USB_FW_ID_AIC8800=ON
endif

ifeq ($(AIC8800_USB_FW_ID_AIC8800D80),y)
    AIC8800_USB_FW_CONF_OPTS += -DAIC8800_USB_FW_ID_AIC8800D80=ON
endif

ifeq ($(AIC8800_USB_FW_ID_AIC8800D80_MINI),y)
    AIC8800_USB_FW_CONF_OPTS += -DAIC8800_USB_FW_ID_AIC8800D80_MINI=ON
endif

ifeq ($(AIC8800_USB_FW_ID_AIC8800D80N),y)
    AIC8800_USB_FW_CONF_OPTS += -DAIC8800_USB_FW_ID_AIC8800D80N=ON
endif

ifeq ($(AIC8800_USB_FW_ID_AIC8800D80X2),y)
    AIC8800_USB_FW_CONF_OPTS += -DAIC8800_USB_FW_ID_AIC8800D80X2=ON
endif

ifeq ($(AIC8800_USB_FW_ID_AIC8800D80DC),y)
    AIC8800_USB_FW_CONF_OPTS += -DAIC8800_USB_FW_ID_AIC8800D80DC=ON
endif

$(eval $(cmake-package))
