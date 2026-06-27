find_package(GTest REQUIRED)
include(GoogleTest)


add_executable(process_communications_tests
        common/process_communications/ErrorTypesTest.cpp
        common/process_communications/InprocTransportTest.cpp
        common/process_communications/TCPTransportTest.cpp)

target_link_libraries(process_communications_tests PRIVATE
        lib::process_communications
        GTest::gtest
        GTest::gtest_main)

gtest_discover_tests(process_communications_tests)
