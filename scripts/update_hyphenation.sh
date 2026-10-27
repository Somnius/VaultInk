#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"

process() {
  local lang="$1"

  mkdir -p "build"
  wget -O "build/$lang.bin" "https://github.com/typst/hypher/raw/refs/heads/main/tries/$lang.bin"

  python scripts/generate_hyphenation_trie.py \
    --input "build/$lang.bin" \
    --output "lib/Epub/Epub/hyphenation/generated/hyph-${lang}.trie.h"
}

process en
process fr
process de
process es
process ru
process it
process uk
process pl
process pt
process sv
process fi
process el

# hyph-el-polyton.trie.h is not in typst/hypher's tries. It was built from
# tex-hyphen's hyph-el-polyton.tex with hypher's tests/generate.rs, by adding
# ("GreekPolytonic", "elpoly", &[], "Grek", "hyph-el-polyton.tex", 1, 1) to its
# language list (hypher d81cc50), then:
#   python scripts/generate_hyphenation_trie.py --input tries/elpoly.bin \
#     --output lib/Epub/Epub/hyphenation/generated/hyph-el-polyton.trie.h
