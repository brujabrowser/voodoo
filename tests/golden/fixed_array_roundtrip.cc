// Proves real-mojom-parity phase 5's codegen path -- `array<T, N>`, a
// fixed-size array with no length prefix on the wire -- by compiling the
// generated fixed_array_gen.h (from
// examples/fixed_array/fixed_array.voodoom) against real
// WASMCadidumBindings and actually round-tripping std::array<T, N>
// values through it: a fixed array of scalars and a fixed array of a
// struct (both inside Grid, sent as a whole struct field), a nullable
// fixed array in both present and absent states, and a fixed array as a
// bare top-level method parameter.
#include "test.h"

#include "fixed_array_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class GridServiceImpl : public fixed_array::GridService {
 public:
  void SendGrid(const fixed_array::Grid& g) override {
    ++send_grid_calls;
    last_grid = g;
  }
  void SendFixedInts(const std::array<int32_t, 5>& vals,
                      base::OnceCallback<void(int32_t)> callback) override {
    int32_t sum = 0;
    for (int32_t v : vals) sum += v;
    callback(sum);
  }

  int send_grid_calls = 0;
  fixed_array::Grid last_grid;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_fixed_array_of_scalars_and_structs_roundtrip) {
  GridServiceImpl impl;
  mojo::Receiver<fixed_array::GridService> receiver(&impl);
  mojo::Remote<fixed_array::GridService> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  fixed_array::Grid g;
  g.corners_x = {10, 20, 30, 40};
  g.points = {fixed_array::Point{1, 2}, fixed_array::Point{3, 4},
              fixed_array::Point{5, 6}};

  remote->SendGrid(g);
  Pump();

  EXPECT_EQ(impl.send_grid_calls, 1);
  EXPECT_EQ(impl.last_grid.corners_x[0], 10);
  EXPECT_EQ(impl.last_grid.corners_x[1], 20);
  EXPECT_EQ(impl.last_grid.corners_x[2], 30);
  EXPECT_EQ(impl.last_grid.corners_x[3], 40);
  EXPECT_EQ(impl.last_grid.points[0].x, 1);
  EXPECT_EQ(impl.last_grid.points[0].y, 2);
  EXPECT_EQ(impl.last_grid.points[1].x, 3);
  EXPECT_EQ(impl.last_grid.points[1].y, 4);
  EXPECT_EQ(impl.last_grid.points[2].x, 5);
  EXPECT_EQ(impl.last_grid.points[2].y, 6);
  EXPECT(!impl.last_grid.tags.has_value());
}

TEST(golden_nullable_fixed_array_present_roundtrips) {
  GridServiceImpl impl;
  mojo::Receiver<fixed_array::GridService> receiver(&impl);
  mojo::Remote<fixed_array::GridService> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  fixed_array::Grid g;
  g.corners_x = {0, 0, 0, 0};
  g.points = {fixed_array::Point{}, fixed_array::Point{}, fixed_array::Point{}};
  g.tags = std::array<std::string, 2>{"alpha", "beta"};

  remote->SendGrid(g);
  Pump();

  EXPECT_EQ(impl.send_grid_calls, 1);
  EXPECT(impl.last_grid.tags.has_value());
  if (impl.last_grid.tags.has_value()) {
    EXPECT_EQ((*impl.last_grid.tags)[0], "alpha");
    EXPECT_EQ((*impl.last_grid.tags)[1], "beta");
  }
}

TEST(golden_nullable_fixed_array_absent_transmits_cleanly) {
  GridServiceImpl impl;
  mojo::Receiver<fixed_array::GridService> receiver(&impl);
  mojo::Remote<fixed_array::GridService> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  fixed_array::Grid g;
  g.corners_x = {1, 1, 1, 1};
  g.points = {fixed_array::Point{}, fixed_array::Point{}, fixed_array::Point{}};
  // g.tags left absent (default-constructed std::optional).

  remote->SendGrid(g);
  Pump();

  EXPECT_EQ(impl.send_grid_calls, 1);
  EXPECT(!impl.last_grid.tags.has_value());

  // The connection must still work normally afterward -- an absent
  // nullable fixed array mustn't have desynced the message stream.
  remote->SendGrid(g);
  Pump();
  EXPECT_EQ(impl.send_grid_calls, 2);
}

TEST(golden_fixed_array_top_level_method_param_roundtrip) {
  GridServiceImpl impl;
  mojo::Receiver<fixed_array::GridService> receiver(&impl);
  mojo::Remote<fixed_array::GridService> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  std::array<int32_t, 5> vals = {1, 2, 3, 4, 5};
  int32_t sum = -1;
  bool got_response = false;
  remote->SendFixedInts(vals, [&](int32_t s) {
    sum = s;
    got_response = true;
  });
  Pump();

  EXPECT(got_response);
  EXPECT_EQ(sum, 15);
}
