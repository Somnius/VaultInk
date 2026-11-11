# Built with the expat the firmware is built with (lib/expat) and the firmware's expat flags
# (platformio.ini: XML_GE=0, XML_CONTEXT_BYTES=0), not the host's.
add_executable(WebDavTest
  WebDavTest.cpp
  RemoteFilesTest.cpp
  ${REPO_ROOT}/lib/WebDav/WebDav.cpp
  ${REPO_ROOT}/lib/RemoteFiles/RemoteFiles.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(WebDavTest BEFORE PRIVATE
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/WebDav
  ${REPO_ROOT}/lib/RemoteFiles
  ${REPO_ROOT}/lib/XmlParserUtils
)

target_compile_definitions(WebDavTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=0
  XML_STATIC
  XML_DEV_URANDOM
  WEBDAV_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

target_link_libraries(WebDavTest PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(WebDavTest)
