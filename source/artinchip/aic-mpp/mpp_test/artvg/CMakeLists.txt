option(BUILD_ARTVG_STRESS_TESTS "Build ArtVG stress tests" ON)

add_definitions(-DDATADIR="${CMAKE_INSTALL_FULL_DATAROOTDIR}/image")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/public)

find_library(LIBDRM libdrm.so)
find_path(VAR xf86drm.h)

file(GLOB APP_SOURCES_PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/public/*.c)
file(GLOB APP_SOURCES_PUBLIC_IMAGE ${CMAKE_CURRENT_SOURCE_DIR}/public/image_c_array/*.c)
file(GLOB APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/artvg_*.c)

set(APP_SOURCES_ALL ${APP_SOURCES})
if(BUILD_ARTVG_STRESS_TESTS)
	# Stress test framework (shared by all stress tests)
	set(STRESS_FRAMEWORK_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/stree_test/artvg_stress_framework.c)

	# Individual stress test executables
	set(STRESS_TEST_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/stree_test/artvg_clear_stress.c
		${CMAKE_CURRENT_SOURCE_DIR}/stree_test/artvg_blit_stress.c
		${CMAKE_CURRENT_SOURCE_DIR}/stree_test/artvg_gradient_stress.c
		${CMAKE_CURRENT_SOURCE_DIR}/stree_test/artvg_affine_stress.c
		${CMAKE_CURRENT_SOURCE_DIR}/stree_test/artvg_projective_stress.c
		${CMAKE_CURRENT_SOURCE_DIR}/stree_test/artvg_path_stress.c
		${CMAKE_CURRENT_SOURCE_DIR}/stree_test/artvg_comprehensive_stress.c
	)

	set(APP_SOURCES_ALL ${APP_SOURCES_ALL} ${STRESS_TEST_SOURCES})
	message(STATUS "ArtVG stress tests enabled")
endif()

foreach(sourcefile ${APP_SOURCES_ALL})
	file(RELATIVE_PATH filename ${CMAKE_CURRENT_SOURCE_DIR} ${sourcefile})
	string( REPLACE ".c" "" file ${filename} )
	# Get filename without path (remove directory prefix)
	get_filename_component(file ${file} NAME)
	list (APPEND bin ${file})

	# Add framework sources for stress test executables
	if(BUILD_ARTVG_STRESS_TESTS)
		add_executable(${file} ${filename} ${APP_SOURCES_PUBLIC} ${APP_SOURCES_PUBLIC_IMAGE} ${STRESS_FRAMEWORK_SOURCES})
	else()
		add_executable(${file} ${filename} ${APP_SOURCES_PUBLIC} ${APP_SOURCES_PUBLIC_IMAGE})
	endif()

	target_link_libraries(${file} artvg ${LIBDRM} m pthread)
	target_include_directories(${file} PUBLIC "${VAR}/drm/")
	set_target_properties(${file} PROPERTIES INSTALL_RPATH "/usr/local/lib")
endforeach(sourcefile ${APP_SOURCES_ALL})

if(DEFINED CMAKE_INSTALL_FULL_LIBDIR)
	foreach(binfile ${bin})
		install(TARGETS ${binfile}
			RUNTIME DESTINATION "${CMAKE_INSTALL_FULL_BINDIR}")
	endforeach(binfile ${bin})
endif() # CMAKE_INSTALL_FULL_LIBDIR

if(DEFINED CMAKE_INSTALL_FULL_DATAROOTDIR)
	install(DIRECTORY image/
		DESTINATION ${CMAKE_INSTALL_FULL_DATAROOTDIR}/image)
endif() # CMAKE_INSTALL_FULL_DATAROOTDIR
