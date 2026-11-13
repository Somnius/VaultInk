add_executable(DevLinkTest
  DevLinkTest.cpp
  DevLinkBenchTest.cpp
  ${REPO_ROOT}/lib/DevLink/DevLink.cpp
  ${REPO_ROOT}/lib/DevLink/DevLinkBench.cpp
)

target_include_directories(DevLinkTest PRIVATE
  ${REPO_ROOT}/lib/DevLink
)

target_link_libraries(DevLinkTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DevLinkTest)
