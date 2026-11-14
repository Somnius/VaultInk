add_executable(PngSlowDecoderTest
  PngSlowDecoderTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/PngSlowDecoder.cpp
  ${REPO_ROOT}/lib/miniz/src/InflateStream.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/hal/HalMemory.cpp
)
target_include_directories(PngSlowDecoderTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../inflate_stream/stubs
  ${REPO_ROOT}/lib/Epub/Epub/converters
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/miniz/src
)
target_link_libraries(PngSlowDecoderTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PngSlowDecoderTest)
