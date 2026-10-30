add_executable(CbzPagesTest CbzPagesTest.cpp ${REPO_ROOT}/lib/Cbz/CbzPages.cpp)
target_include_directories(CbzPagesTest PRIVATE ${REPO_ROOT}/lib/Cbz)
target_link_libraries(CbzPagesTest PRIVATE GTest::gtest_main)
gtest_discover_tests(CbzPagesTest)
