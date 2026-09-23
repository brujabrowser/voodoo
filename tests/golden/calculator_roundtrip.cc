// Proves the v7 codegen path -- the `[Sync]` method attribute -- by
// compiling the generated calculator_interface_gen.h (from
// examples/calculator/calculator.voodoom) against real
// WASMCadidumBindings and actually running it. The key thing every test
// here checks: no test calls Pump()/RunUntilIdle() itself before reading
// the sync call's result -- if the blocking overload weren't real (if it
// just queued the request and returned early), these EXPECTs would see a
// default-constructed answer, not the actual one.
#include "test.h"

#include "calculator_interface_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class CalculatorImpl : public calculator::Calculator {
 public:
  void Add(int32_t a, int32_t b,
           base::OnceCallback<void(int32_t)> callback) override {
    callback(a + b);
  }
  void Divide(int32_t a, int32_t b,
              base::OnceCallback<void(bool, int32_t)> callback) override {
    if (b == 0) {
      callback(false, 0);
      return;
    }
    callback(true, a / b);
  }
  void Echo(const std::string& value,
            base::OnceCallback<void(std::string)> callback) override {
    callback(value);
  }
};

}  // namespace

TEST(golden_sync_add_blocks_and_returns_correct_result) {
  CalculatorImpl impl;
  mojo::Receiver<calculator::Calculator> receiver(&impl);
  mojo::Remote<calculator::Calculator> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  int32_t sum = -1;
  bool ok = remote.proxy()->Add(2, 3, &sum);
  // Deliberately no whp::Executor::Current().RunUntilIdle() call anywhere
  // above this line -- Add() itself must have done all the pumping.
  EXPECT(ok);
  EXPECT_EQ(sum, 5);
}

TEST(golden_sync_divide_by_zero_still_returns_via_response) {
  CalculatorImpl impl;
  mojo::Receiver<calculator::Calculator> receiver(&impl);
  mojo::Remote<calculator::Calculator> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  bool divide_ok = false;
  int32_t quotient = -999;
  bool call_ok = remote.proxy()->Divide(10, 0, &divide_ok, &quotient);
  EXPECT(call_ok);       // the *call* succeeded (a response came back)...
  EXPECT(!divide_ok);    // ...but the impl's own answer says "not ok"
  EXPECT_EQ(quotient, 0);
}

TEST(golden_multiple_sync_calls_in_a_row_on_the_same_remote) {
  CalculatorImpl impl;
  mojo::Receiver<calculator::Calculator> receiver(&impl);
  mojo::Remote<calculator::Calculator> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  int32_t r1 = 0, r2 = 0, r3 = 0;
  EXPECT(remote.proxy()->Add(1, 1, &r1));
  EXPECT(remote.proxy()->Add(r1, 10, &r2));
  EXPECT(remote.proxy()->Add(r2, 100, &r3));
  EXPECT_EQ(r1, 2);
  EXPECT_EQ(r2, 12);
  EXPECT_EQ(r3, 112);
}

TEST(golden_sync_and_async_methods_coexist_on_one_interface) {
  CalculatorImpl impl;
  mojo::Receiver<calculator::Calculator> receiver(&impl);
  mojo::Remote<calculator::Calculator> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  // The async method still needs its own pump -- proves [Sync] on Add()/
  // Divide() didn't somehow make the whole interface synchronous.
  std::string got;
  bool echo_done = false;
  remote->Echo("hello", [&](std::string v) {
    got = v;
    echo_done = true;
  });
  EXPECT(!echo_done);  // not yet -- nothing pumped
  for (int i = 0; i < 6; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
  EXPECT(echo_done);
  EXPECT_EQ(got, "hello");

  // And the sync method on the same Remote still blocks correctly
  // afterward.
  int32_t sum = 0;
  EXPECT(remote.proxy()->Add(4, 5, &sum));
  EXPECT_EQ(sum, 9);
}
