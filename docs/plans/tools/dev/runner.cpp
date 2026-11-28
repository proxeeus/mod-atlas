#include <gtest/gtest.h>
int main() { for (auto& t : reg()) { int before = fails(); t.fn(); std::printf("%s %s\n", fails() == before ? "ok  " : "FAIL", t.name); } return fails() ? 1 : 0; }
