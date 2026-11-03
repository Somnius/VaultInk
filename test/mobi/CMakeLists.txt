add_executable(MobiTest
  MobiTest.cpp
  ${REPO_ROOT}/lib/Mobi/MobiFormat.cpp
  ${REPO_ROOT}/lib/Mobi/MobiHtmlFilter.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
)

target_include_directories(MobiTest PRIVATE
  ${REPO_ROOT}/lib/Mobi
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(MobiTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MobiTest)
