find_package(EXPAT REQUIRED)

add_executable(MarkupBooksTest
  ChmBookTest.cpp
  CodePageTest.cpp
  DocToHtmlTest.cpp
  Fb2ToHtmlTest.cpp
  HostileBooksTest.cpp
  HtmlBookTest.cpp
  RtfToHtmlTest.cpp
  ${REPO_ROOT}/lib/Chm/ChmBook.cpp
  ${REPO_ROOT}/lib/Chm/Lzx.cpp
  ${REPO_ROOT}/lib/CodePage/CodePage.cpp
  ${REPO_ROOT}/lib/Doc/DocToHtml.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2ToHtml.cpp
  ${REPO_ROOT}/lib/Html/HtmlBook.cpp
  ${REPO_ROOT}/lib/Rtf/RtfToHtml.cpp
  ${REPO_ROOT}/lib/Mobi/MobiHtmlFilter.cpp
  ${REPO_ROOT}/lib/Mobi/MobiFormat.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
)

target_include_directories(MarkupBooksTest PRIVATE
  ${REPO_ROOT}/lib/Chm
  ${REPO_ROOT}/lib/CodePage
  ${REPO_ROOT}/lib/Doc
  ${REPO_ROOT}/lib/Fb2
  ${REPO_ROOT}/lib/Html
  ${REPO_ROOT}/lib/Rtf
  ${REPO_ROOT}/lib/Mobi
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/XmlParserUtils
)

target_compile_definitions(MarkupBooksTest PRIVATE
  MARKUP_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_link_libraries(MarkupBooksTest PRIVATE
  crosspoint_test_common
  EXPAT::EXPAT
  GTest::gtest_main
)

gtest_discover_tests(MarkupBooksTest)

# A command-line converter for trying real .doc and .chm files on this computer (see the file's head). Not a test.
add_executable(MarkupConvert
  MarkupConvertTool.cpp
  ${REPO_ROOT}/lib/Chm/ChmBook.cpp
  ${REPO_ROOT}/lib/Chm/Lzx.cpp
  ${REPO_ROOT}/lib/CodePage/CodePage.cpp
  ${REPO_ROOT}/lib/Doc/DocToHtml.cpp
  ${REPO_ROOT}/lib/Html/HtmlBook.cpp
  ${REPO_ROOT}/lib/Mobi/MobiHtmlFilter.cpp
  ${REPO_ROOT}/lib/Mobi/MobiFormat.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
)

target_include_directories(MarkupConvert PRIVATE
  ${REPO_ROOT}/lib/Chm
  ${REPO_ROOT}/lib/CodePage
  ${REPO_ROOT}/lib/Doc
  ${REPO_ROOT}/lib/Html
  ${REPO_ROOT}/lib/Mobi
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(MarkupConvert PRIVATE
  crosspoint_test_common
)

# The FB2 tests again with the expat the firmware is built with (lib/expat) and the firmware's expat flags
# (platformio.ini: XML_GE=0, XML_CONTEXT_BYTES=0), not the host's: the memory cap and the encodings depend on it.
add_executable(Fb2FirmwareExpatTest
  Fb2ToHtmlTest.cpp
  ${REPO_ROOT}/lib/CodePage/CodePage.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2ToHtml.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(Fb2FirmwareExpatTest BEFORE PRIVATE
  ${REPO_ROOT}/lib/expat
  ${REPO_ROOT}/lib/CodePage
  ${REPO_ROOT}/lib/Fb2
  ${REPO_ROOT}/lib/XmlParserUtils
)

target_compile_definitions(Fb2FirmwareExpatTest PRIVATE
  XML_GE=0
  XML_CONTEXT_BYTES=0
  XML_STATIC
  XML_DEV_URANDOM
)

target_link_libraries(Fb2FirmwareExpatTest PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(Fb2FirmwareExpatTest TEST_PREFIX "FirmwareExpat.")
