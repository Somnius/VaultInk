add_executable(HomeRowOrderTest
  HomeRowOrderTest.cpp
)

target_link_libraries(HomeRowOrderTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeRowOrderTest)
