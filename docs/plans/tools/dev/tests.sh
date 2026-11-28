#!/bin/zsh
# Runs mod-atlas's unit tests (tests/*.cpp, pure headers only) with a tiny
# stand-in for gtest, no build of AC needed. Prints failures and the ok count.
T=${0:A:h}
M=/Users/ac-playerbots/azerothcore-playerbots/azerothcore-wotlk/modules/mod-atlas
SDK="/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk"
OUT=${TMPDIR:-/tmp}/atlas-tests
cd $T && /usr/bin/clang++ -std=c++20 -Wall -Wextra -nostdinc++ -isystem "$SDK/usr/include/c++/v1" -isysroot "$SDK" \
    -I. -Istub -I$M/src -include stub/Define.h runner.cpp $M/tests/*.cpp -o $OUT || exit 1
$OUT | grep -v "^ok"
echo "ok: $($OUT | grep -c '^ok')"
