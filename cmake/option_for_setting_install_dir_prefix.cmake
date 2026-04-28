# Determined by value of INSTALL_ON_SYSTEM option

if(NOT INSTALL_ON_SYSTEM)
	# If it's OFF, install path is a directory "install" in the top-level CMake project
	set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Default install directory prefix" FORCE)
endif()