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

# The developer's own builds (VAULTINK_DEVLINK + VAULTINK_DEVLINK_FULL, env x4pro-dev): KEY, TAP,
# SWIPE, FLASH, LS, GET, PUT and FETCH, the input queue, the SD path rule and the URL guard.
add_executable(DevLinkDevTest
  DevLinkDevTest.cpp
  DevLinkFetchTest.cpp
  ${REPO_ROOT}/lib/DevLink/DevLink.cpp
  ${REPO_ROOT}/lib/DevLink/DevLinkArgs.cpp
  ${REPO_ROOT}/lib/DevLink/DevLinkFetch.cpp
  ${REPO_ROOT}/lib/DevLink/DevLinkInput.cpp
)

target_compile_definitions(DevLinkDevTest PRIVATE VAULTINK_DEVLINK=1 VAULTINK_DEVLINK_FULL=1)

target_include_directories(DevLinkDevTest PRIVATE
  ${REPO_ROOT}/lib/DevLink
  ${REPO_ROOT}/src
)

target_link_libraries(DevLinkDevTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DevLinkDevTest)

# The read-only link of the build users may install (VAULTINK_DEVLINK + VAULTINK_DEVLINK_USER, env
# x4pro-dev-user): the commands that act on the reader are not in its table.
add_executable(DevLinkUserTest
  DevLinkUserTest.cpp
  ${REPO_ROOT}/lib/DevLink/DevLink.cpp
)

target_compile_definitions(DevLinkUserTest PRIVATE VAULTINK_DEVLINK=1 VAULTINK_DEVLINK_USER=1)

target_include_directories(DevLinkUserTest PRIVATE
  ${REPO_ROOT}/lib/DevLink
)

target_link_libraries(DevLinkUserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DevLinkUserTest)
