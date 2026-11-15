add_executable(FormatFeaturesTest
  FormatFeaturesTest.cpp
)

target_include_directories(FormatFeaturesTest PRIVATE ${REPO_ROOT}/src)

target_link_libraries(FormatFeaturesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FormatFeaturesTest)
