add_executable(AutoScheduleTest
  AutoScheduleTest.cpp
)

target_include_directories(AutoScheduleTest PRIVATE
  ${REPO_ROOT}/lib/AutoSchedule
)

target_link_libraries(AutoScheduleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AutoScheduleTest)
