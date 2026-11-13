// Converts a .doc or .chm on this computer the way the reader does and writes the XHTML page to stdout, for
// trying real files without a device (not a test, and not run by ctest):
//   MarkupConvert book.chm > book.html
// To compare lib/Chm/Lzx with another decoder, it also decodes raw LZX frames: the file with the compressed
// bytes, the window bits, the size they unpack to, and where each frame starts in the file (frames must start
// at a reset):
//   MarkupConvert --lzx content.bin 16 6881 0 > plain.bin

#include <ChmBook.h>
#include <DocToHtml.h>
#include <Lzx.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string readAll(const char* path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream data;
  data << in.rdbuf();
  return data.str();
}

int readAt(void* ctx, uint32_t offset, uint8_t* buf, size_t size) {
  const auto* data = static_cast<const std::string*>(ctx);
  if (offset >= data->size()) return 0;
  const size_t n = size < data->size() - offset ? size : data->size() - offset;
  memcpy(buf, data->data() + offset, n);
  return static_cast<int>(n);
}

bool toStdout(void*, const char* data, size_t size) { return fwrite(data, 1, size, stdout) == size; }

bool endsWith(const std::string& text, const char* end) {
  const size_t n = strlen(end);
  return text.size() >= n && strcasecmp(text.c_str() + text.size() - n, end) == 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 6 && strcmp(argv[1], "--lzx") == 0) {
    const std::string packed = readAll(argv[2]);
    const size_t size = strtoul(argv[4], nullptr, 10);
    std::vector<size_t> starts;
    for (int i = 5; i < argc; i++) starts.push_back(strtoul(argv[i], nullptr, 10));
    lzx::Decoder decoder;
    if (!decoder.init(static_cast<unsigned>(atoi(argv[3])))) return 2;
    for (size_t f = 0; f < starts.size(); f++) {
      const size_t to = f + 1 < starts.size() ? starts[f + 1] : packed.size();
      const size_t done = f * lzx::FRAME;
      const uint32_t want = static_cast<uint32_t>(size - done < lzx::FRAME ? size - done : lzx::FRAME);
      const uint8_t* out = nullptr;
      const lzx::Result result =
          decoder.decodeFrame(reinterpret_cast<const uint8_t*>(packed.data()) + starts[f], to - starts[f], want, out);
      if (result != lzx::Result::Ok) {
        fprintf(stderr, "frame %zu: %s\n", f, result == lzx::Result::Unsupported ? "unsupported" : "corrupt");
        return 1;
      }
      fwrite(out, 1, want, stdout);
    }
    return 0;
  }
  if (argc != 2) {
    fprintf(stderr, "usage: %s file.doc|file.chm > page.html\n", argv[0]);
    return 2;
  }
  std::string data = readAll(argv[1]);
  const blocksource::Source source{&data, readAt, static_cast<uint32_t>(data.size())};
  const std::string path = argv[1];
  if (endsWith(path, ".chm")) {
    chm::Info info;
    chm::Status status = chm::readInfo(source, info);
    fprintf(stderr, "info: %s, title \"%s\", language \"%s\"\n", chm::statusText(status), info.title.c_str(),
            info.language.c_str());
    if (status != chm::Status::Ok) return 1;
    status = chm::streamHtml(source, "", toStdout, nullptr);
    fprintf(stderr, "page: %s\n", chm::statusText(status));
    return status == chm::Status::Ok || status == chm::Status::Truncated ? 0 : 1;
  }
  doc::Info info;
  doc::Status status = doc::readInfo(source, info);
  fprintf(stderr, "info: %s, title \"%s\", author \"%s\", language \"%s\"\n", doc::statusText(status),
          info.title.c_str(), info.author.c_str(), info.language.c_str());
  if (status != doc::Status::Ok) return 1;
  status = doc::streamHtml(source, info.title, toStdout, nullptr);
  fprintf(stderr, "page: %s\n", doc::statusText(status));
  return status == doc::Status::Ok || status == doc::Status::Truncated ? 0 : 1;
}
