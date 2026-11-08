add_executable(MenuLayoutTest
  MenuLayoutTest.cpp
)

target_include_directories(MenuLayoutTest PRIVATE ${REPO_ROOT}/src)

target_link_libraries(MenuLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MenuLayoutTest)
