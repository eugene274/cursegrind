cmake_minimum_required(VERSION 3.20)
project(callgrind_viewer)

set(CMAKE_CXX_STANDARD 17)


find_package(Curses REQUIRED)
find_package(Boost REQUIRED)
find_package (Threads REQUIRED)

add_executable(callgrind_viewer main.cpp)
target_include_directories(callgrind_viewer PRIVATE
        ${CURSES_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS})
target_link_libraries(callgrind_viewer PRIVATE
        ${CURSES_LIBRARIES}
        ${BOOST_LIBRARIES}
        Threads::Threads
        )

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(callgrind_viewer_tests CallgrindParser.test.cpp)
target_compile_options(callgrind_viewer_tests PUBLIC -O0 -g -ggdb)
target_include_directories(callgrind_viewer_tests PRIVATE
        ${CURSES_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS})
target_link_libraries(callgrind_viewer_tests PRIVATE
        ${CURSES_LIBRARIES}
        ${BOOST_LIBRARIES}
        gtest_main
        )