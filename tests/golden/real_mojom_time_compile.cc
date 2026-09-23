// Compiles a real Chromium time.mojom through voodoomc against live wcb.
#include "test.h"

#include "time_mojom_gen.h"

TEST(real_mojom_time_generates_and_compiles_against_wcb) {
  mojo_base::mojom::Time t;
  t.internal_value = 42;
  EXPECT_EQ(t.internal_value, 42);
  mojo_base::mojom::TimeDelta d;
  d.microseconds = 7;
  EXPECT_EQ(d.microseconds, 7);
}
