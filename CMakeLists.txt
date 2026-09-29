cmake_minimum_required(VERSION 3.21)

# ---- Cache build vars ----
macro(set_from_environment VARIABLE)
	if (NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} $ENV{${VARIABLE}})
	endif ()
endmacro()

# Post-build copy is on only when a deploy directory is given (environment, -D or gen.ps1):
# a hardcoded Mod Organizer path that does not exist on this machine would just break the build.
set_from_environment(STB_PROJECTILECONTROL_DEPLOY_DIR)
if (DEFINED STB_PROJECTILECONTROL_DEPLOY_DIR AND NOT "${STB_PROJECTILECONTROL_DEPLOY_DIR}" STREQUAL "")
	set(STB_PROJECTILECONTROL_DEPLOY_DIR "${STB_PROJECTILECONTROL_DEPLOY_DIR}" CACHE PATH "Directory to copy the built plugin and PDB into.")
	option(COPY_BUILD "Copy the build output to STB_PROJECTILECONTROL_DEPLOY_DIR." ON)
else ()
	option(COPY_BUILD "Copy the build output to STB_PROJECTILECONTROL_DEPLOY_DIR." OFF)
endif ()

# ---- Vcpkg check ----
set_from_environment(VCPKG_ROOT)
if (NOT DEFINED VCPKG_ROOT)
	message(
		FATAL_ERROR
		"Variable VCPKG_ROOT is not set."
	)
endif ()

# ---- Include guards ----
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(
		FATAL_ERROR
			"In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
	)
endif()

set(Boost_USE_STATIC_RUNTIME OFF CACHE BOOL "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
set(STB_PROJECTILECONTROL_VERSION "1.0.0" CACHE STRING "Project version override")

# ---- Project ----

project(
	STB_ProjectileControl
	VERSION ${STB_PROJECTILECONTROL_VERSION}
	LANGUAGES CXX
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	@ONLY
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	@ONLY
)

# ---- Globals ----

if (MSVC)
	add_compile_definitions(
		_UNICODE
	)

	if (NOT ${CMAKE_GENERATOR} STREQUAL "Ninja")
		add_compile_options(
			/MP	# Build with Multiple Processes
		)
	endif ()
endif ()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

# ---- Dependencies ----

# CommonLibSSE-NG: one DLL for SE 1.5.97, AE 1.6.x and 1.7.x.
#
# Only the local checkout pointed at by CommonLibSSEPath_NG: it knows Address Library
# format 5 and classifies 1.7.x as AE. The vcpkg port does neither, so a 1.7.99 game would
# be treated as SE and every RELOCATION_ID would resolve to the SE id.
set_from_environment(CommonLibSSEPath_NG)
if (NOT DEFINED CommonLibSSEPath_NG OR NOT EXISTS "${CommonLibSSEPath_NG}")
	message(
		FATAL_ERROR
		"CommonLibSSEPath_NG is not set or does not exist -- point it at a CommonLibSSE-NG checkout."
	)
endif ()
message(STATUS "CommonLibSSE-NG from: ${CommonLibSSEPath_NG}")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CommonLibSSEPath_NG}" ./CommonLibSSE/build)

find_package(spdlog REQUIRED CONFIG)

# ---- Add source files ----

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

source_group(
	TREE
		${CMAKE_CURRENT_SOURCE_DIR}
	FILES
		${SOURCES}
)

source_group(
	TREE
		${CMAKE_CURRENT_BINARY_DIR}
	FILES
		${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
)

# ---- Create DLL ----

add_library(
	${PROJECT_NAME}
	SHARED
	${SOURCES}
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	.clang-format
	.editorconfig
)

target_compile_features(
	${PROJECT_NAME}
	PRIVATE
		cxx_std_23
)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		spdlog::spdlog
)

if (MSVC)
	target_compile_options(
		${PROJECT_NAME}
		PRIVATE
			/sdl	# Enable Additional Security Checks
			/utf-8	# Set Source and Executable character sets to UTF-8
			/Zi	# Debug Information Format

			/permissive-	# Standards conformance

			/Zc:alignedNew	# C++17 over-aligned allocation
			/Zc:auto	# Deduce Variable Type
			/Zc:char8_t
			/Zc:__cplusplus	# Enable updated __cplusplus macro
			/Zc:externC
			/Zc:externConstexpr	# Enable extern constexpr variables
			/Zc:forScope	# Force Conformance in for Loop Scope
			/Zc:hiddenFriend
			/Zc:implicitNoexcept	# Implicit Exception Specifiers
			/Zc:lambda
			/Zc:noexceptTypes	# C++17 noexcept rules
			/Zc:preprocessor	# Enable preprocessor conformance mode
			/Zc:referenceBinding	# Enforce reference binding rules
			/Zc:rvalueCast	# Enforce type conversion rules
			/Zc:sizedDealloc	# Enable Global Sized Deallocation Functions
			/Zc:strictStrings	# Disable string literal type conversion
			/Zc:ternary	# Enforce conditional operator rules
			/Zc:threadSafeInit	# Thread-safe Local Static Initialization
			/Zc:tlsGuards
			/Zc:trigraphs	# Trigraphs Substitution
			/Zc:wchar_t	# wchar_t Is Native Type

			/external:anglebrackets
			/external:W0

			/W4	# Warning level
			/WX	# Warning level (warnings are errors)

			"$<$<CONFIG:DEBUG>:>"
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
	)

	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			/WX	# Treat Linker Warnings as Errors

			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif ()

target_precompile_headers(
	${PROJECT_NAME}
	PRIVATE
		src/PCH.h
)

if (COPY_BUILD)
	message(STATUS "Post-build copy destination: ${STB_PROJECTILECONTROL_DEPLOY_DIR}")
	add_custom_command(
		TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${STB_PROJECTILECONTROL_DEPLOY_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${PROJECT_NAME}> "${STB_PROJECTILECONTROL_DEPLOY_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_PDB_FILE:${PROJECT_NAME}> "${STB_PROJECTILECONTROL_DEPLOY_DIR}"
	)
endif ()
