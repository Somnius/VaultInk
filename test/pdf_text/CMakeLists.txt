add_executable(PdfTextTest
  PdfTextTest.cpp
  ${REPO_ROOT}/lib/Pdf/PdfText.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
)
target_include_directories(PdfTextTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../inflate_stream/stubs
  ${REPO_ROOT}/lib/Pdf
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/miniz/src
)
target_compile_definitions(PdfTextTest PRIVATE PDF_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(PdfTextTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PdfTextTest)
