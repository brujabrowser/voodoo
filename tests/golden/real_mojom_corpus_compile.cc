// Compiles the rest of the vendored Chromium .mojom corpus against live wcb.
#include "test.h"

#include "application_state_mojom_gen.h"
#include "base/i18n/rtl.h"
#include "battery_monitor_mojom_gen.h"
#include "file_path_mojom_gen.h"
#include "generic_pending_associated_receiver_mojom_gen.h"
#include "generic_pending_receiver_mojom_gen.h"
#include "geoposition_mojom_gen.h"
#include "process_id_mojom_gen.h"
#include "read_only_buffer_mojom_gen.h"
#include "text_direction_mojom_gen.h"

TEST(real_mojom_text_direction_matches_base_i18n) {
  EXPECT_EQ(static_cast<int>(mojo_base::mojom::TextDirection::LEFT_TO_RIGHT),
            static_cast<int>(base::i18n::LEFT_TO_RIGHT));
  EXPECT_EQ(static_cast<int>(mojo_base::mojom::TextDirection::RIGHT_TO_LEFT),
            static_cast<int>(base::i18n::RIGHT_TO_LEFT));
}

TEST(real_mojom_battery_status_compiles_against_wcb) {
  // BatteryStatus comes in via battery_monitor's import (battery_status_gen.h).
  device::mojom::BatteryStatus s;
  s.charging = true;
  s.level = 1.0;
  EXPECT(s.charging);
}

TEST(real_mojom_generic_pending_receiver_compiles_against_wcb) {
  mojo_base::mojom::GenericPendingReceiver r;
  r.interface_name = "echo.Echo";
  EXPECT(r.interface_name == "echo.Echo");
}

TEST(real_mojom_geoposition_compiles_against_wcb) {
  device::mojom::Geoposition g;
  g.latitude = 1.0;
  EXPECT_EQ(g.latitude, 1.0);
}

TEST(real_mojom_battery_monitor_interface_compiles_against_wcb) {
  EXPECT_EQ(device::mojom::BatteryMonitor::kQueryNextStatusName, 0u);
}

TEST(real_mojom_application_state_compiles_against_wcb) {
  EXPECT_EQ(static_cast<int>(mojo_base::mojom::ApplicationState::UNKNOWN), 0);
}

TEST(real_mojom_process_id_compiles_against_wcb) {
  mojo_base::mojom::ProcessId p;
  p.pid = 1u;
  EXPECT_EQ(p.pid, 1u);
}

TEST(real_mojom_read_only_buffer_compiles_against_wcb) {
  mojo_base::mojom::ReadOnlyBuffer b;
  EXPECT(b.buffer.empty());
}

TEST(real_mojom_file_path_enable_if_string_arm_compiles) {
  mojo_base::mojom::FilePath p;
  p.path = "a";
  EXPECT(p.path == "a");
}

TEST(real_mojom_generic_pending_associated_receiver_compiles_against_wcb) {
  EXPECT_EQ(mojo_base::mojom::GenericAssociatedInterface::kVersion, 0u);
}
