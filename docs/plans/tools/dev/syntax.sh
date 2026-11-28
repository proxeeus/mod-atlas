#!/bin/zsh
# Syntax-only check of mod-atlas sources against the real AC headers (no build).
#   tools/dev/syntax.sh                 every module source
#   tools/dev/syntax.sh src/AtlasMgr.cpp ...
# Prints only errors and warnings in the module; exit 1 if any.
R=/Users/ac-playerbots/azerothcore-playerbots/azerothcore-wotlk
M=$R/modules/mod-atlas
SDK="/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
RSP=${TMPDIR:-/tmp}/atlas-inc.rsp
# src/tools has its own Config.h, deps' msvc/zlib/openssl dirs shadow std headers.
(find $R/src -type d -not -path "*/src/tools*" -not -path "*/src/test*"; find $R/deps -type d -not -path "*/.git*"; \
 find $M/src -type d) | grep -v "msvc\|/win\|jemalloc\|/zlib\|bzip2\|openssl" | sed 's/^/-I/' > $RSP
cd $M
files=("$@"); [ ${#files} -eq 0 ] && files=(src/*.cpp src/scripts/*.cpp)
fail=0
for f in $files; do
    out=$(/usr/bin/clang++ -std=c++20 -fsyntax-only -nostdinc++ -isystem "$SDK/usr/include/c++/v1" -isysroot "$SDK" \
        -isystem /opt/homebrew/include @$RSP -Wall -Wextra -Wno-unused-parameter $f 2>&1); rc=$?
    mine=$(echo "$out" | grep -E "(error|warning):" | grep -v "^$R/src\|deps/\|SDKs")
    if [ $rc -ne 0 ] || [ -n "$mine" ]; then echo "== $f (rc=$rc)"; echo "$mine" | head -10; fail=1; fi
done
[ $fail -eq 0 ] && echo "syntax: clean (${#files} files)"
exit $fail
