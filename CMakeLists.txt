cmake_minimum_required(VERSION 3.16)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)	# required for editor

# C++ Compiler Settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(BUILD_SHARED_LIBS OFF)
option(INSTALL_ON_SYSTEM "Install binary to the system" OFF)

# Set CMake Module Path
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Module To Set Up Dependencies
find_package(PkgConfig REQUIRED)
pkg_search_module(LIBEVDEV REQUIRED libevdev)
pkg_search_module(LIBUDEV REQUIRED libudev)
pkg_check_modules(LIBSYSTEMD REQUIRED libsystemd)



# Project Compile Instructions
##################################################################################
project(xboxmapper
	VERSION 0
	DESCRIPTION "HID inputs using an X-Box controller"
	HOMEPAGE_URL "https://github.com/KevJin97/linux-xbox-remapper"
	LANGUAGES CXX
)
# Include CMake Modules Defined In The "./cmake" Directory
	include(set_project_dependencies)
	include(option_for_setting_install_dir_prefix)

# Compile Project Library
	add_library(${PROJECT_LIB_NAME} ${PROJECT_SOURCES})
	include(set_library_bin_output_naming_format)
	target_include_directories(${PROJECT_LIB_NAME} PUBLIC ${PROJECT_INCLUDE_DIRS})
	target_link_libraries(${PROJECT_LIB_NAME} PUBLIC ${PROJECT_LIBRARIES})

# Compile Project Executable
	add_executable(${PROJECT_EXEC_NAME} src/main.cpp)
	target_link_libraries(${PROJECT_EXEC_NAME} PRIVATE ${PROJECT_LIB_NAME})

# Set Installation Paths Relative to 
	include(set_relative_install_paths)
##################################################################################



# Custom Function
add_custom_target(uninstall
	COMMAND sh -c "echo 'Uninstalling xboxmapper...'; xargs rm < ${CMAKE_BINARY_DIR}/install_manifest.txt;"
	VERBATIM
)
