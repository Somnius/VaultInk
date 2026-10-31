add_executable(BatteryLogTest
  BatteryLogTest.cpp
  ${REPO_ROOT}/lib/BatteryLog/BatteryLog.cpp
)

target_include_directories(BatteryLogTest PRIVATE
  ${REPO_ROOT}/lib/BatteryLog
)

target_link_libraries(BatteryLogTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BatteryLogTest)
