cmake_minimum_required(VERSION 3.10)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(tddsockets VERSION 0.5)

add_executable(tddsockets ${tddsockets_SOURCE_DIR}/src/tests/main.cpp ${tddsockets_SOURCE_DIR}/src/xpcommon.cpp)
target_include_directories(tddsockets PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/tests)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
target_link_libraries(tddsockets pthread)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GCC")
target_link_libraries(tddsockets pthread)
endif()

