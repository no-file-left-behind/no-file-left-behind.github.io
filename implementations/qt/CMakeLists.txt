cmake_minimum_required(VERSION 3.16)
project(nflb_qt VERSION 0.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Built on its own, this folder also builds the example and the tests. Added to an app with
# add_subdirectory(), it builds only the library unless asked.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(NFLB_TOP_LEVEL ON)
else()
    set(NFLB_TOP_LEVEL OFF)
endif()
option(NFLB_BUILD_EXAMPLE "Build the example app" ${NFLB_TOP_LEVEL})
option(NFLB_BUILD_TESTS "Build the tests" ${NFLB_TOP_LEVEL})

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)

add_library(nflb STATIC
    src/closingmodel.h
    src/closingmodel.cpp
    src/unsavedlistdialog.h
    src/unsavedlistdialog.cpp
)
target_include_directories(nflb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nflb PUBLIC Qt6::Widgets)

if(NFLB_BUILD_EXAMPLE)
    add_executable(nflb_example example/main.cpp)
    target_link_libraries(nflb_example PRIVATE nflb)
endif()

if(NFLB_BUILD_TESTS)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    enable_testing()

    add_executable(tst_scenarios tests/tst_scenarios.cpp)
    target_link_libraries(tst_scenarios PRIVATE nflb Qt6::Test)
    target_compile_definitions(tst_scenarios PRIVATE
        NFLB_SCENARIOS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/../../scenarios/scenarios.json")
    add_test(NAME scenarios COMMAND tst_scenarios)

    add_executable(tst_dialog tests/tst_dialog.cpp)
    target_link_libraries(tst_dialog PRIVATE nflb Qt6::Test)
    add_test(NAME dialog COMMAND tst_dialog)
    # The dialog test needs no display.
    set_tests_properties(dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(scenarios dialog PROPERTIES TIMEOUT 60)
endif()
