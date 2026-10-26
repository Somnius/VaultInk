add_executable(VaultMarkdownTest
  MarkdownToHtmlTest.cpp
  ${REPO_ROOT}/lib/Vault/MarkdownToHtml.cpp
)

target_include_directories(VaultMarkdownTest PRIVATE
  ${REPO_ROOT}/lib/Vault
)

target_link_libraries(VaultMarkdownTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  expat
)

gtest_discover_tests(VaultMarkdownTest)
