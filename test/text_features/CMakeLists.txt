add_executable(TextFeaturesTest
  TextFeaturesTest.cpp
)

target_include_directories(TextFeaturesTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Epub/Epub
)

target_link_libraries(TextFeaturesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TextFeaturesTest)
