add_executable(HomeRowLayoutTest
  HomeRowLayoutTest.cpp
)

target_link_libraries(HomeRowLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeRowLayoutTest)
