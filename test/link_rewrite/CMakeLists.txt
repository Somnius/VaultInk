add_executable(LinkRewriteTest LinkRewriteTest.cpp ${REPO_ROOT}/lib/Vault/LinkRewrite.cpp)
target_include_directories(LinkRewriteTest PRIVATE ${REPO_ROOT}/lib/Vault)
target_link_libraries(LinkRewriteTest PRIVATE GTest::gtest_main)
gtest_discover_tests(LinkRewriteTest)
