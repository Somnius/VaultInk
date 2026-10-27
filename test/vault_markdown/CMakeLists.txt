add_executable(VaultMarkdownTest
  MarkdownToHtmlTest.cpp
  VaultLinkTest.cpp
  TextFoldTest.cpp
  VaultIndexTest.cpp
  CanvasTest.cpp
  TaskToggleTest.cpp
  LineBufferTest.cpp
  ${REPO_ROOT}/lib/Vault/MarkdownToHtml.cpp
  ${REPO_ROOT}/lib/Vault/VaultLink.cpp
  ${REPO_ROOT}/lib/Vault/TextFold.cpp
  ${REPO_ROOT}/lib/Vault/VaultIndex.cpp
  ${REPO_ROOT}/lib/Vault/Canvas.cpp
  ${REPO_ROOT}/lib/Vault/TaskToggle.cpp
  ${REPO_ROOT}/lib/Vault/LineBuffer.cpp
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
