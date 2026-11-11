add_executable(FtpTest
  FtpTest.cpp
  ${REPO_ROOT}/lib/Ftp/Ftp.cpp
  ${REPO_ROOT}/lib/RemoteFiles/RemoteFiles.cpp
)

target_include_directories(FtpTest PRIVATE
  ${REPO_ROOT}/lib/Ftp
  ${REPO_ROOT}/lib/RemoteFiles
)

target_link_libraries(FtpTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FtpTest)
