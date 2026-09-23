// Proves real-mojom-parity phase 6's codegen path -- `result<T, E>`
// response types, desugared to a synthesized two-arm union -- by
// compiling the generated result_type_gen.h (from
// examples/result_type/result_type.voodoom) against real
// WASMCadidumBindings and actually running it: Inventory.Get exercises
// an async result<Item, NotFoundError> response in both the success and
// error arm, and the [Sync] Divide exercises result<T, E> alongside a
// real blocking call (matching calculator_roundtrip.cc's proof that the
// blocking overload does all its own pumping -- no RunUntilIdle() call
// anywhere near the sync call itself).
#include "test.h"

#include "result_type_gen.h"
#include "base/expected.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class InventoryImpl : public result_type::Inventory {
 public:
  void Get(const std::string& key,
           base::OnceCallback<void(
               base::expected<result_type::Item, result_type::NotFoundError>)>
               callback) override {
    if (key == "apple") {
      result_type::Item item;
      item.name = "apple";
      item.quantity = 7;
      callback(std::move(item));
    } else {
      result_type::NotFoundError err;
      err.message = "no such item: " + key;
      callback(base::unexpected(std::move(err)));
    }
  }

  void Divide(int32_t a, int32_t b,
              base::OnceCallback<void(base::expected<int32_t, std::string>)>
                  callback) override {
    if (b == 0) {
      callback(base::unexpected(std::string("division by zero")));
    } else {
      callback(a / b);
    }
  }
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_result_response_success_arm_roundtrips) {
  InventoryImpl impl;
  mojo::Receiver<result_type::Inventory> receiver(&impl);
  mojo::Remote<result_type::Inventory> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  base::expected<result_type::Item, result_type::NotFoundError> got{
      result_type::Item{}};
  bool done = false;
  remote->Get("apple", [&](auto r) {
    got = std::move(r);
    done = true;
  });
  Pump();

  EXPECT(done);
  EXPECT(got.has_value());
  EXPECT_EQ(got.value().name, "apple");
  EXPECT_EQ(got.value().quantity, 7);
}

TEST(golden_result_response_error_arm_roundtrips) {
  InventoryImpl impl;
  mojo::Receiver<result_type::Inventory> receiver(&impl);
  mojo::Remote<result_type::Inventory> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  base::expected<result_type::Item, result_type::NotFoundError> got{
      result_type::Item{}};
  bool done = false;
  remote->Get("banana", [&](auto r) {
    got = std::move(r);
    done = true;
  });
  Pump();

  EXPECT(done);
  EXPECT(!got.has_value());
  EXPECT_EQ(got.error().message, "no such item: banana");
}

TEST(golden_sync_result_response_success_arm_blocks_and_returns) {
  InventoryImpl impl;
  mojo::Receiver<result_type::Inventory> receiver(&impl);
  mojo::Remote<result_type::Inventory> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  base::expected<int32_t, std::string> result{0};
  bool call_ok = remote.proxy()->Divide(10, 2, &result);
  // Deliberately no RunUntilIdle() call anywhere above this line.
  EXPECT(call_ok);
  EXPECT(result.has_value());
  EXPECT_EQ(result.value(), 5);
}

TEST(golden_sync_result_response_error_arm_blocks_and_returns) {
  InventoryImpl impl;
  mojo::Receiver<result_type::Inventory> receiver(&impl);
  mojo::Remote<result_type::Inventory> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  base::expected<int32_t, std::string> result{0};
  bool call_ok = remote.proxy()->Divide(10, 0, &result);
  EXPECT(call_ok);  // the *call* succeeded (a response came back)...
  EXPECT(!result.has_value());
  EXPECT_EQ(result.error(), "division by zero");
}
