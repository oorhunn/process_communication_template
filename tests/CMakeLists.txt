find_package(GTest REQUIRED)
include(GoogleTest)

# Mirror the source layout under tests/: each test target lives at the same
# relative path as the header it exercises (common/process_communications/...).
add_executable(process_communications_tests
        common/process_communications/ErrorTypesTest.cpp
        common/process_communications/InprocTransportTest.cpp
        common/process_communications/TCPTransportTest.cpp)

target_include_directories(process_communications_tests PRIVATE ${ROOT_PATH})

target_link_libraries(process_communications_tests PRIVATE GTest::gtest GTest::gtest_main)

gtest_discover_tests(process_communications_tests)
