# use build date as patch version
string(TIMESTAMP BUILD_TIME "%Y%m%d")
set(CPACK_PACKAGE_VERSION_PATCH "${BUILD_TIME}")
set(CPACK_PACKAGE_NAME "libftl-protocol")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}")
set(CPACK_DEBIAN_PACKAGE_NAME "FTL Protocol Library")
set(CPACK_PACKAGE_VENDOR "University of Turku")
set(CPACK_PACKAGE_DESCRIPTION "Networking and streaming library for FTL data")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "UTU Future Tech Lab")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS_POLICY ">=")
#set(CPACK_DEB_PACKAGE_COMPONENT ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "Miscellaneous")

#install(DIRECTORY "${PROJECT_SOURCE_DIR}/data/" DESTINATION "${CMAKE_INSTALL_PREFIX}/share/ftl/data/")

macro(deb_append_dependency DEPENDS)
	if ("${CPACK_DEBIAN_PACKAGE_DEPENDS}" STREQUAL "")
		set(CPACK_DEBIAN_PACKAGE_DEPENDS "${DEPENDS}")
	else()
		set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, ${DEPENDS}")
	endif()
endmacro()

#if (HAVE_PYLON)
#	deb_append_dependency("pylon (>= 6.1.1)")
#	set(ENV{LD_LIBRARY_PATH} "=/opt/pylon/lib/")
#endif()

deb_append_dependency("libmsgpackc2 (>= 3.0.1-3)")
deb_append_dependency("liburiparser1 (>= 0.9.3-2)")
deb_append_dependency("libgnutlsxx28 (>= 3.6.13)")

if(WIN32)
	#message(STATUS "Copying DLLs: OpenCV")
	#file(GLOB WINDOWS_LIBS "${OpenCV_INSTALL_PATH}/${OpenCV_ARCH}/${OpenCV_RUNTIME}/bin/*.dll")
	#install(FILES ${WINDOWS_LIBS} DESTINATION bin)
	set(CPACK_GENERATOR "ZIP")
else()
	set(CPACK_GENERATOR "DEB")
endif()

include(CPack)
