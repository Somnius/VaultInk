add_executable(ReadingPresetsTest
  ReadingPresetsTest.cpp
  ${REPO_ROOT}/lib/ReadingPresets/ReadingPresets.cpp
)

target_include_directories(ReadingPresetsTest PRIVATE
  ${REPO_ROOT}/lib/ReadingPresets
)

target_link_libraries(ReadingPresetsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingPresetsTest)
