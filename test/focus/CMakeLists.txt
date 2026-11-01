add_executable(FocusTest
  FocusTest.cpp
  ${REPO_ROOT}/lib/Focus/Focus.cpp
)

target_include_directories(FocusTest PRIVATE
  ${REPO_ROOT}/lib/Focus
)

target_link_libraries(FocusTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FocusTest)
