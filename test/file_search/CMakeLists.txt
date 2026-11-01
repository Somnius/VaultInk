add_executable(FileSearchTest
  FileSearchTest.cpp
  ${REPO_ROOT}/lib/FileSearch/FileSearch.cpp
  ${REPO_ROOT}/lib/Vault/TextFold.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(FileSearchTest PRIVATE
  ${REPO_ROOT}/lib/FileSearch
  ${REPO_ROOT}/lib/Vault
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(FileSearchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FileSearchTest)
