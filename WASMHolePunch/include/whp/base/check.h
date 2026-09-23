#ifndef WHP_BASE_CHECK_H_
#define WHP_BASE_CHECK_H_

#include <cstdio>
#include <cstdlib>

#define WHP_CHECK(cond)                                                        \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__,     \
                   #cond);                                                     \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

#ifndef NDEBUG
#define WHP_DCHECK(cond) WHP_CHECK(cond)
#else
#define WHP_DCHECK(cond) ((void)0)
#endif

#endif  // WHP_BASE_CHECK_H_
