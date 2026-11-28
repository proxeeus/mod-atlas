#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
struct TestReg { const char* name; void (*fn)(); };
inline std::vector<TestReg>& reg() { static std::vector<TestReg> r; return r; }
inline int& fails() { static int f = 0; return f; }
struct Sink_ { template<typename T> Sink_& operator<<(T const&) { return *this; } };
#define TEST(a, b) static void a##_##b(); static int a##_##b##_r = (reg().push_back({#a "." #b, a##_##b}), 0); static void a##_##b()
#define ACHECK_(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails(); return; } } while (0)
#define ECHECK_(c) if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails(); } else Sink_()
#define ASSERT_EQ(a, b) ACHECK_((a) == (b))
#define ASSERT_NE(a, b) ACHECK_((a) != (b))
#define ASSERT_GE(a, b) ACHECK_((a) >= (b))
#define ASSERT_LE(a, b) ACHECK_((a) <= (b))
#define ASSERT_TRUE(a) ACHECK_(a)
#define ASSERT_FALSE(a) ACHECK_(!(a))
#define EXPECT_EQ(a, b) ECHECK_((a) == (b))
#define EXPECT_NE(a, b) ECHECK_((a) != (b))
#define EXPECT_GT(a, b) ECHECK_((a) > (b))
#define EXPECT_GE(a, b) ECHECK_((a) >= (b))
#define EXPECT_LT(a, b) ECHECK_((a) < (b))
#define EXPECT_LE(a, b) ECHECK_((a) <= (b))
#define EXPECT_TRUE(a) ECHECK_(a)
#define EXPECT_FALSE(a) ECHECK_(!(a))
#define EXPECT_STREQ(a, b) ECHECK_(std::strcmp((a), (b)) == 0)
#define EXPECT_NEAR(a, b, e) ECHECK_(std::fabs(double(a) - double(b)) <= double(e))
#define EXPECT_FLOAT_EQ(a, b) ECHECK_(std::fabs((a) - (b)) <= 1e-5f * std::max(1.0f, std::fabs(float(b))))
