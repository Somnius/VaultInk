add_executable(HabitsTest
  HabitsTest.cpp
  ${REPO_ROOT}/lib/Habits/Habits.cpp
)

target_include_directories(HabitsTest PRIVATE
  ${REPO_ROOT}/lib/Habits
)

target_link_libraries(HabitsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HabitsTest)
