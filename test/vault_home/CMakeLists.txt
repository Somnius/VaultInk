add_executable(VaultHomeTest
  VaultHomeTest.cpp
  ${REPO_ROOT}/lib/Vault/VaultHome.cpp
  ${REPO_ROOT}/lib/Vault/VaultLink.cpp
)

target_include_directories(VaultHomeTest PRIVATE
  ${REPO_ROOT}/lib/Vault
)

target_link_libraries(VaultHomeTest PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(VaultHomeTest)
