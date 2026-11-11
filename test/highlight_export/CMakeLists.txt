add_executable(HighlightExportTest
  HighlightExportTest.cpp
  ${REPO_ROOT}/lib/HighlightExport/HighlightExport.cpp
)

target_include_directories(HighlightExportTest PRIVATE
  ${REPO_ROOT}/lib/HighlightExport
)

target_link_libraries(HighlightExportTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HighlightExportTest)
