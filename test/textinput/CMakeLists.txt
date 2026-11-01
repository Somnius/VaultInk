add_executable(TextInputTest
  TextInputTest.cpp
  ${REPO_ROOT}/lib/TextInput/TextInput.cpp
)

target_include_directories(TextInputTest PRIVATE
  ${REPO_ROOT}/lib/TextInput
)

find_package(Threads REQUIRED)

target_link_libraries(TextInputTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  Threads::Threads
)

gtest_discover_tests(TextInputTest)
