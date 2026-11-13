# What every build has: the line reader, the command table as the shipped builds compile it, the
# replies and the bench log's pure parts.
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

# The developer build's part (VAULTINK_DEVLINK, env x4pro-dev): KEY, TAP, SWIPE, FLASH, LS, GET and
# PUT, the input queue and the SD path rule.
add_executable(DevLinkDevTest
  DevLinkDevTest.cpp
  ${REPO_ROOT}/lib/DevLink/DevLink.cpp
  ${REPO_ROOT}/lib/DevLink/DevLinkArgs.cpp
  ${REPO_ROOT}/lib/DevLink/DevLinkInput.cpp
)

target_compile_definitions(DevLinkDevTest PRIVATE VAULTINK_DEVLINK=1)

target_include_directories(DevLinkDevTest PRIVATE
  ${REPO_ROOT}/lib/DevLink
  ${REPO_ROOT}/src
)

target_link_libraries(DevLinkDevTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DevLinkDevTest)
