cmake_minimum_required(VERSION 3.17)
project(ige)

if (POLICY CMP0054)
    cmake_policy(SET CMP0054 NEW)
endif()

if (POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

if(${PROJECT_NAME} STREQUAL ${CMAKE_PROJECT_NAME})
	option(IGE_BUILD_TESTS "If the tests should be built" ON)
	option(IGE_BUILD_EXAMPLES "If the examples should be built" ON)
else()
	option(IGE_BUILD_TESTS "If the tests should be built" OFF)
	option(IGE_BUILD_EXAMPLES "If the examples should be built" OFF)
endif()

add_subdirectory(ige)

if(IGE_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

# tests
include(CTest)

if(IGE_BUILD_TESTS AND BUILD_TESTING)
	enable_testing()
	add_subdirectory(tests)
endif()
