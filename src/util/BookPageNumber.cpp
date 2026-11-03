#include "BookPageNumber.h"

namespace bookpage {
namespace {
int current = 0;
}  // namespace

void set(const int page) { current = page; }
int number() { return current; }
}  // namespace bookpage
