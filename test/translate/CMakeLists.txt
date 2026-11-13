add_executable(TranslateTest
  TranslateTest.cpp
  ${REPO_ROOT}/lib/Translate/Translate.cpp
)

target_include_directories(TranslateTest PRIVATE
  ${REPO_ROOT}/lib/Translate
)

target_link_libraries(TranslateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TranslateTest)
