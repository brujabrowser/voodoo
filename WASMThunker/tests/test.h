#ifndef WST_TESTS_TEST_H_
#define WST_TESTS_TEST_H_

#include <cstdio>
#include <vector>

struct TestCase {
  const char* name;
  void (*fn)();
};

inline std::vector<TestCase>& Tests() {
  static std::vector<TestCase> tests;
  return tests;
}

struct TestReg {
  TestReg(const char* name, void (*fn)()) { Tests().push_back({name, fn}); }
};

#define TEST(name)                                                             \
  void test_##name();                                                          \
  static TestReg reg_##name(#name, test_##name);                               \
  void test_##name()

extern int g_failures;

#define EXPECT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

#define EXPECT_EQ(a, b) EXPECT((a) == (b))

#endif  // WST_TESTS_TEST_H_
