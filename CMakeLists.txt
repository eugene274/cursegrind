cmake_minimum_required(VERSION 3.20)
project(cursegrind)

set(CMAKE_CXX_STANDARD 17)

option(${PROJECT_NAME}_BUILD_TESTS "Build tests" OFF)


find_package(Curses REQUIRED)
find_package(Boost REQUIRED)
#find_package (Threads REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CURSES_INCLUDE_DIRS}
        ${Boost_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE
        ${CURSES_LIBRARIES}
        ${BOOST_LIBRARIES}
#        Threads::Threads
        )

if (${PROJECT_NAME}_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/609281088cfefc76f9d0ce82e1ff6c30cc3591e5.zip
    )
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(${PROJECT_NAME}_tests CallgrindParser.test.cpp)
    target_compile_options(${PROJECT_NAME}_tests PUBLIC -O0 -g -ggdb)
    target_include_directories(${PROJECT_NAME}_tests PRIVATE
            ${CURSES_INCLUDE_DIRS}
            ${Boost_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME}_tests PRIVATE
            ${CURSES_LIBRARIES}
            ${BOOST_LIBRARIES}
            gtest_main
            )
endif ()

