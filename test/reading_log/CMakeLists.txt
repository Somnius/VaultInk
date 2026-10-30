add_executable(ReadingLogTest
  ReadingLogTest.cpp
  ${REPO_ROOT}/lib/ReadingLog/ReadingLog.cpp
)

target_include_directories(ReadingLogTest PRIVATE
  ${REPO_ROOT}/lib/ReadingLog
)

target_link_libraries(ReadingLogTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingLogTest)
