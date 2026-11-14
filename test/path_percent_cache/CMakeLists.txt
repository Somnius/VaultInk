add_executable(PathPercentCacheTest
  PathPercentCacheTest.cpp
  FailedKeysTest.cpp
)

target_link_libraries(PathPercentCacheTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PathPercentCacheTest)
