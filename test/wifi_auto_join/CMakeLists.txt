add_executable(WifiAutoJoinTest
  WifiAutoJoinTest.cpp
)

target_link_libraries(WifiAutoJoinTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WifiAutoJoinTest)
