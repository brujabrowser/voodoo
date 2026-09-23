#include "test.h"

#include "whp/base/expected.h"
#include "whp/base/once_callback.h"
#include "whp/base/repeating_callback.h"

#include <string>
#include <utility>

TEST(expected_holds_value_or_error) {
  whp::expected<int, std::string> ok(42);
  EXPECT(ok.has_value());
  EXPECT_EQ(ok.value(), 42);

  whp::expected<int, std::string> bad(whp::unexpected<std::string>("nope"));
  EXPECT(!bad.has_value());
  EXPECT_EQ(bad.error(), std::string("nope"));
}

TEST(once_callback_invokes_and_consumes) {
  int n = 0;
  whp::OnceCallback<void(int)> cb([&](int x) { n = x; });
  EXPECT(!cb.is_null());
  cb(3);
  EXPECT_EQ(n, 3);
  EXPECT(cb.is_null());
}

TEST(once_callback_holds_move_only_capture) {
  int n = 0;
  whp::OnceCallback<void(int)> inner([&](int x) { n = x; });
  whp::OnceCallback<bool()> outer(
      [cb = std::move(inner)]() mutable {
        std::move(cb).Run(7);
        return true;
      });
  EXPECT(!outer.is_null());
  bool r = std::move(outer).Run();
  EXPECT(r);
  EXPECT_EQ(n, 7);
  EXPECT(outer.is_null());
}

TEST(bind_once_forwards_move_only_callback) {
  int n = 0;
  whp::OnceCallback<void(int)> inner([&](int x) { n = x; });
  whp::OnceClosure bound = whp::BindOnce(std::move(inner), 5);
  std::move(bound).Run();
  EXPECT_EQ(n, 5);
}

TEST(repeating_callback_can_run_twice) {
  int n = 0;
  whp::RepeatingCallback<void(int)> cb([&](int x) { n += x; });
  cb.Run(2);
  cb.Run(3);
  EXPECT_EQ(n, 5);
}
