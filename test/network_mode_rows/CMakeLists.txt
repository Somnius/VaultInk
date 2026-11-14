add_executable(NetworkModeRowsTest
  NetworkModeRowsTest.cpp
)

target_link_libraries(NetworkModeRowsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NetworkModeRowsTest)
