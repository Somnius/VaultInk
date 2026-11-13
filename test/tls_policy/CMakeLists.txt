add_executable(TlsPolicyTest
  TlsPolicyTest.cpp
  ${REPO_ROOT}/lib/TlsPolicy/TlsPolicy.cpp
)

target_include_directories(TlsPolicyTest PRIVATE
  ${REPO_ROOT}/lib/TlsPolicy
)

target_link_libraries(TlsPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TlsPolicyTest)

# The same tests with GitHub's check switched on, as env:x4pro-dev-verify builds it.
add_executable(TlsPolicyVerifyGithubTest
  TlsPolicyTest.cpp
  ${REPO_ROOT}/lib/TlsPolicy/TlsPolicy.cpp
)

target_compile_definitions(TlsPolicyVerifyGithubTest PRIVATE VAULTINK_VERIFY_GITHUB=1)

target_include_directories(TlsPolicyVerifyGithubTest PRIVATE
  ${REPO_ROOT}/lib/TlsPolicy
)

target_link_libraries(TlsPolicyVerifyGithubTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TlsPolicyVerifyGithubTest TEST_PREFIX VerifyGithub.)
