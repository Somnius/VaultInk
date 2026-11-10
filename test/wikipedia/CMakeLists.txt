add_executable(WikipediaTest
  WikipediaTest.cpp
  ${REPO_ROOT}/lib/Wikipedia/Wikipedia.cpp
)

target_include_directories(WikipediaTest PRIVATE
  ${REPO_ROOT}/lib/Wikipedia
)

target_link_libraries(WikipediaTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WikipediaTest)
