cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

set(GITLABNSS_VERSION 0.9.4)

project(gitlabnss VERSION ${GITLABNSS_VERSION} LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)

##########################################################################################
# Debian Package
##########################################################################################
# Debian Packaging only available if this is the main app
if((CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME))
	include(GNUInstallDirs)

    set(CMAKE_INSTALL_PREFIX /) # Everything is an absolute path
    set(CPACK_PACKAGING_INSTALL_PREFIX /) # Everything is an absolute path
    set(CMAKE_INSTALL_LIBDIR "/usr/lib")
    set(CMAKE_INSTALL_FULL_LIBDIR "/usr/lib")

	install(TARGETS nss_gitlab
		ARCHIVE DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR}
			COMPONENT gitlabnss
        PERMISSIONS PERMISSIONS OWNER_READ OWNER_WRITE #?? GROUP_READ GROUP_WRITE WORLD_READ WORLD_WRITE
    )
    install(TARGETS authorized_keys gitlabnssd
        RUNTIME DESTINATION "/bin" # ${CMAKE_INSTALL_FULL_BINDIR}
            COMPONENT gitlabnss
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    )
    install(FILES res/gitlabnssd
        DESTINATION "/etc/init.d"
            COMPONENT gitlabnss
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
    )
    install(FILES "res/gitlabnss.conf"
        DESTINATION "/etc/gitlabnss"
            COMPONENT gitlabnss
        PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
    )
    
	SET(CPACK_GENERATOR "DEB")
	SET(CPACK_PACKAGE_NAME "gitlabnss")
	SET(CPACK_SET_DESTDIR TRUE)
	SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "not-set")
	SET(CPACK_PACKAGE_VERSION ${GITLABNSS_VERSION})
	SET(CPACK_DEBIAN_PACKAGE_DESCRIPTION "GitLab login for NSS")
	# SET(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
	# SET(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
	SET(CPACK_DEBIAN_PACKAGE_DEPENDS "")
	SET(CPACK_PACKAGE_VENDOR "")
	include(CPack)
	cpack_add_component(gitlabnss)
endif()