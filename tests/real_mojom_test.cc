// (real-mojom-parity phase 8/9) Real-.mojom-file regression corpus --
// parses a handful of small, real, *unmodified* Chromium .mojom files
// (vendored verbatim under tests/real_mojom/, mirroring their real
// chromium/src path so import resolution needs nothing but a single
// --import-dir-equivalent root) through the real LoadModuleGraph/parser
// pipeline, proving actual real-file compatibility -- not just synthetic
// grammar tests built to exercise one feature at a time. Each file was
// fetched from
// https://chromium.googlesource.com/chromium/src/+/main/<path>?format=TEXT
// (base64-decoded) during this phase's own work, and two of them
// (services/device/public/mojom/battery_status.mojom's
// `double.INFINITY` field default, and geoposition.mojom's
// `mojo_base.mojom.Time` cross-file dotted type reference) directly
// surfaced real grammar gaps this phase fixed -- see
// DefaultValue::float_special in ast.h and ParseTypeSpecInner's
// generalized dotted-name handling in parser.cc.
//
// This proves *parsing* against unmodified Chromium files. Generated
// headers also compile against wcb in tests/golden/real_mojom_* (time,
// values, text_direction via base::i18n::TextDirection, battery, geoposition,
// generic_pending_receiver). Chromium typemaps (base::Time as a native
// class) are not applied -- generated structs carry the mojom fields.
#include "test.h"

#include "../src/module_loader.h"

#include <fstream>
#include <sstream>
#include <unordered_set>

using namespace voodoom;

namespace {

// WVC_REAL_MOJOM_DIR is injected by CMakeLists.txt as an absolute path to
// this file's own directory's real_mojom/ subfolder -- see
// target_compile_definitions(wvc_tests ...) -- so this works regardless
// of the test binary's current working directory.
std::optional<std::string> ReadRealMojomFile(const std::string& path) {
  std::ifstream in(std::string(WVC_REAL_MOJOM_DIR) + "/" + path,
                    std::ios::binary);
  if (!in.is_open()) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST(real_mojom_time_parses) {
  // mojo/public/mojom/base/time.mojom -- dotted `module mojo_base.mojom;`,
  // four self-contained structs, [Stable] attributes (accepted/ignored).
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/time.mojom", {}, ReadRealMojomFile);
  EXPECT_EQ(graph.size(), 1u);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.name, "mojo_base.mojom");
  EXPECT_EQ(m.structs.size(), 4u);
}

TEST(real_mojom_text_direction_parses) {
  // mojo/public/mojom/base/text_direction.mojom -- a plain top-level enum.
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/text_direction.mojom", {}, ReadRealMojomFile);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.enums.size(), 1u);
  EXPECT_EQ(m.enums[0].name, "TextDirection");
  EXPECT_EQ(m.enums[0].values.size(), 3u);
}

TEST(real_mojom_battery_status_parses_with_infinity_default) {
  // services/device/public/mojom/battery_status.mojom -- the file that
  // surfaced the `double.INFINITY` field-default gap this phase fixed.
  LoadedGraph graph = LoadModuleGraph(
      "services/device/public/mojom/battery_status.mojom", {},
      ReadRealMojomFile);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.structs.size(), 1u);
  const StructDecl& s = m.structs[0];
  EXPECT_EQ(s.name, "BatteryStatus");
  EXPECT_EQ(s.fields.size(), 4u);
  const StructField& discharging_time = s.fields[2];
  EXPECT_EQ(discharging_time.name, "discharging_time");
  EXPECT(discharging_time.default_value.has_value);
  EXPECT(discharging_time.default_value.float_special ==
         DefaultValue::FloatSpecial::kInfinity);
}

TEST(real_mojom_battery_monitor_parses_with_import) {
  // services/device/public/mojom/battery_monitor.mojom imports
  // battery_status.mojom by its real, full chromium/src-relative path --
  // proving that resolves correctly against a single import_dirs root
  // (the vendored corpus's own top).
  LoadedGraph graph = LoadModuleGraph(
      "services/device/public/mojom/battery_monitor.mojom", {"."},
      ReadRealMojomFile);
  EXPECT_EQ(graph.size(), 2u);
  const Module& entry = graph.back().module;
  EXPECT_EQ(entry.interfaces.size(), 1u);
  EXPECT_EQ(entry.interfaces[0].name, "BatteryMonitor");
  EXPECT_EQ(entry.interfaces[0].methods[0].name, "QueryNextStatus");
  EXPECT(entry.interfaces[0].methods[0].has_response);
  EXPECT(entry.interfaces[0].methods[0].response_params[0].type.kind ==
         TypeKind::kStructRef);
  EXPECT_EQ(entry.interfaces[0].methods[0].response_params[0].type.name,
            "BatteryStatus");
}

TEST(real_mojom_generic_pending_receiver_parses_handle_struct_field) {
  // mojo/public/mojom/base/generic_pending_receiver.mojom -- a real
  // Chromium file with a handle<message_pipe> struct field, field
  // ordinals (`name@0`), and a [Stable] struct attribute. This is the
  // file that would have failed before v23 allowed handle-bearing fields.
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/generic_pending_receiver.mojom", {},
      ReadRealMojomFile);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.name, "mojo_base.mojom");
  EXPECT_EQ(m.structs.size(), 1u);
  EXPECT_EQ(m.structs[0].name, "GenericPendingReceiver");
  EXPECT_EQ(m.structs[0].fields.size(), 2u);
  EXPECT_EQ(m.structs[0].fields[0].name, "interface_name");
  EXPECT(m.structs[0].fields[0].has_explicit_ordinal);
  EXPECT_EQ(m.structs[0].fields[0].ordinal, 0u);
  EXPECT_EQ(m.structs[0].fields[1].name, "receiving_pipe");
  EXPECT(m.structs[0].fields[1].type.kind == TypeKind::kHandleMessagePipe);
  EXPECT_EQ(m.structs[0].fields[1].ordinal, 1u);
}

TEST(real_mojom_unguessable_token_parses) {
  // mojo/public/mojom/base/unguessable_token.mojom -- about as small as a
  // real vendored file gets: one [Stable] struct, two uint64 fields, no
  // imports, no interface. A baseline sanity check alongside the more
  // structurally interesting files below.
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/unguessable_token.mojom", {}, ReadRealMojomFile);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.name, "mojo_base.mojom");
  EXPECT_EQ(m.structs.size(), 1u);
  EXPECT_EQ(m.structs[0].name, "UnguessableToken");
  EXPECT_EQ(m.structs[0].fields.size(), 2u);
  EXPECT_EQ(m.structs[0].fields[0].name, "high");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kUint64);
  EXPECT_EQ(m.structs[0].fields[1].name, "low");
}

TEST(real_mojom_token_parses) {
  // mojo/public/mojom/base/token.mojom -- structurally identical to
  // UnguessableToken above (two uint64 fields) but a distinct real file,
  // confirming there's nothing UnguessableToken-specific being matched.
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/token.mojom", {}, ReadRealMojomFile);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.structs.size(), 1u);
  EXPECT_EQ(m.structs[0].name, "Token");
  EXPECT_EQ(m.structs[0].fields.size(), 2u);
}

TEST(real_mojom_big_buffer_parses_with_shared_buffer_handle) {
  // mojo/public/mojom/base/big_buffer.mojom -- a [Stable] union
  // (array<uint8> / BigBufferSharedMemoryRegion struct / bool arms) where
  // one arm is itself a struct carrying a handle<shared_buffer> field --
  // exercises v23's handle-bearing-struct-field support nested one level
  // inside a union, a combination none of the synthetic examples happen
  // to hit together.
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/big_buffer.mojom", {}, ReadRealMojomFile);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.structs.size(), 1u);
  EXPECT_EQ(m.structs[0].name, "BigBufferSharedMemoryRegion");
  EXPECT_EQ(m.structs[0].fields.size(), 2u);
  EXPECT_EQ(m.structs[0].fields[0].name, "buffer_handle");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kHandleSharedBuffer);
  EXPECT_EQ(m.unions.size(), 1u);
  const UnionDecl& u = m.unions[0];
  EXPECT_EQ(u.name, "BigBuffer");
  EXPECT_EQ(u.fields.size(), 3u);
  EXPECT_EQ(u.fields[0].name, "bytes");
  EXPECT(u.fields[0].type.kind == TypeKind::kArray);
  EXPECT_EQ(u.fields[1].name, "shared_memory");
  EXPECT(u.fields[1].type.kind == TypeKind::kStructRef);
  EXPECT_EQ(u.fields[1].type.name, "BigBufferSharedMemoryRegion");
  EXPECT_EQ(u.fields[2].name, "invalid_buffer");
}

TEST(real_mojom_string16_parses_with_cross_file_union_field) {
  // mojo/public/mojom/base/string16.mojom imports big_buffer.mojom and
  // uses its BigBuffer union (not just a struct, unlike geoposition's
  // Time) as an ordinary struct field type -- proves cross-file
  // resolution works for an imported union the same way it already does
  // for an imported struct.
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/string16.mojom", {"."}, ReadRealMojomFile);
  EXPECT_EQ(graph.size(), 2u);
  const Module& entry = graph.back().module;
  EXPECT_EQ(entry.structs.size(), 2u);
  EXPECT_EQ(entry.structs[0].name, "String16");
  EXPECT(entry.structs[0].fields[0].type.kind == TypeKind::kArray);
  EXPECT_EQ(entry.structs[1].name, "BigString16");
  const StructField& data = entry.structs[1].fields[0];
  EXPECT_EQ(data.name, "data");
  EXPECT(data.type.kind == TypeKind::kUnionRef);
  EXPECT_EQ(data.type.name, "BigBuffer");
  EXPECT_EQ(data.type.owner_namespace, "mojo_base.mojom");
}

TEST(real_mojom_geometry_parses_many_structs_with_embedding) {
  // ui/gfx/geometry/mojom/geometry.mojom -- fifteen self-contained
  // structs, several (QuadF, AxisTransform2d) embedding earlier ones by
  // value -- the real-file equivalent of this compiler's own
  // struct-declaration-order check, at a volume none of the synthetic
  // examples exercise (checked pairwise, not just one embed).
  LoadedGraph graph = LoadModuleGraph(
      "ui/gfx/geometry/mojom/geometry.mojom", {}, ReadRealMojomFile);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.name, "gfx.mojom");
  EXPECT_EQ(m.structs.size(), 15u);
  const StructDecl* quad = nullptr;
  const StructDecl* axis = nullptr;
  for (const StructDecl& s : m.structs) {
    if (s.name == "QuadF") quad = &s;
    if (s.name == "AxisTransform2d") axis = &s;
  }
  EXPECT(quad != nullptr);
  if (quad) {
    EXPECT_EQ(quad->fields.size(), 4u);
    for (const StructField& f : quad->fields) {
      EXPECT(f.type.kind == TypeKind::kStructRef);
      EXPECT_EQ(f.type.name, "PointF");
    }
  }
  EXPECT(axis != nullptr);
  if (axis) {
    EXPECT_EQ(axis->fields.size(), 2u);
    EXPECT_EQ(axis->fields[0].name, "scale");
    EXPECT(axis->fields[0].type.kind == TypeKind::kStructRef);
    EXPECT_EQ(axis->fields[0].type.name, "Vector2dF");
  }
}

TEST(real_mojom_sensor_parses_fixed_array_and_no_response_methods) {
  // services/device/public/mojom/sensor.mojom -- a real
  // `array<double, 4> values;` fixed-size struct field (v19) at a
  // different N than any synthetic example uses, plus two interfaces
  // whose methods are a mix of response-bearing (GetDefaultConfiguration,
  // AddConfiguration) and fire-and-forget (RemoveConfiguration, Suspend,
  // Resume, ConfigureReadingChangeNotifications, RaiseError,
  // SensorReadingChanged) -- proving both method shapes parse correctly
  // side by side in one real interface, not just in isolation.
  LoadedGraph graph = LoadModuleGraph(
      "services/device/public/mojom/sensor.mojom", {}, ReadRealMojomFile);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.name, "device.mojom");
  EXPECT_EQ(m.enums.size(), 2u);
  EXPECT_EQ(m.enums[0].name, "SensorType");
  EXPECT_EQ(m.enums[0].values.size(), 10u);

  const StructDecl* raw = nullptr;
  for (const StructDecl& s : m.structs) {
    if (s.name == "SensorReadingRaw") raw = &s;
  }
  EXPECT(raw != nullptr);
  if (raw) {
    EXPECT_EQ(raw->fields.size(), 2u);
    const StructField& values = raw->fields[1];
    EXPECT_EQ(values.name, "values");
    EXPECT(values.type.kind == TypeKind::kArray);
    EXPECT_EQ(values.type.fixed_array_size, 4);
    EXPECT(values.type.element->kind == TypeKind::kDouble);
  }

  EXPECT_EQ(m.interfaces.size(), 2u);
  const Interface* sensor = nullptr;
  const Interface* client = nullptr;
  for (const Interface& i : m.interfaces) {
    if (i.name == "Sensor") sensor = &i;
    if (i.name == "SensorClient") client = &i;
  }
  EXPECT(sensor != nullptr);
  if (sensor) {
    EXPECT_EQ(sensor->methods.size(), 6u);
    const Method& get_default = sensor->methods[0];
    EXPECT_EQ(get_default.name, "GetDefaultConfiguration");
    EXPECT(get_default.has_response);
    const Method& remove_config = sensor->methods[2];
    EXPECT_EQ(remove_config.name, "RemoveConfiguration");
    EXPECT(!remove_config.has_response);
  }
  EXPECT(client != nullptr);
  if (client) {
    EXPECT_EQ(client->methods.size(), 2u);
    EXPECT(!client->methods[0].has_response);
  }
}

TEST(real_mojom_geoposition_parses_with_dotted_cross_file_type) {
  // services/device/public/mojom/geoposition.mojom -- the file that
  // surfaced the generalized-dotted-name (`mojo_base.mojom.Time`) gap
  // this phase fixed; also exercises const, struct, enum, and union
  // top-level decls together with field defaults referencing earlier
  // consts.
  LoadedGraph graph = LoadModuleGraph(
      "services/device/public/mojom/geoposition.mojom", {"."},
      ReadRealMojomFile);
  EXPECT_EQ(graph.size(), 2u);
  const Module& entry = graph.back().module;
  EXPECT_EQ(entry.consts.size(), 7u);
  EXPECT_EQ(entry.structs.size(), 2u);
  EXPECT_EQ(entry.enums.size(), 1u);
  EXPECT_EQ(entry.unions.size(), 1u);

  const StructDecl& geo = entry.structs[0];
  EXPECT_EQ(geo.name, "Geoposition");
  const StructField* timestamp = nullptr;
  for (const StructField& f : geo.fields) {
    if (f.name == "timestamp") timestamp = &f;
  }
  EXPECT(timestamp != nullptr);
  if (timestamp) {
    EXPECT(timestamp->type.kind == TypeKind::kStructRef);
    EXPECT_EQ(timestamp->type.name, "Time");
    EXPECT_EQ(timestamp->type.owner_namespace, "mojo_base.mojom");
  }
}

TEST(real_mojom_values_parses_mutually_recursive_union_and_structs) {
  // mojo/public/mojom/base/values.mojom -- union Value has by-value
  // DictionaryValue/ListValue fields, and those structs reference Value
  // back through map<string, Value> / array<Value>. CageVector/CageMap
  // (WASMSafeSpace) instantiate while Value is still incomplete, so this
  // parses; the generator emits DictionaryValue/ListValue first.
  LoadedGraph g = LoadModuleGraph("mojo/public/mojom/base/values.mojom", {},
                                  ReadRealMojomFile);
  EXPECT_EQ(g.size(), 1u);
  const Module& entry = g.back().module;
  EXPECT_EQ(entry.unions.size(), 1u);
  EXPECT_EQ(entry.unions[0].name, "Value");
  EXPECT_EQ(entry.structs.size(), 2u);
  EXPECT_EQ(entry.structs[0].name, "DictionaryValue");
  EXPECT_EQ(entry.structs[1].name, "ListValue");
}

TEST(real_mojom_application_state_parses) {
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/application_state.mojom", {},
      ReadRealMojomFile);
  EXPECT_EQ(graph[0].module.enums.size(), 1u);
  EXPECT_EQ(graph[0].module.enums[0].name, "ApplicationState");
  EXPECT_EQ(graph[0].module.enums[0].values.size(), 5u);
}

TEST(real_mojom_process_id_parses) {
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/process_id.mojom", {}, ReadRealMojomFile);
  EXPECT_EQ(graph[0].module.structs.size(), 1u);
  EXPECT_EQ(graph[0].module.structs[0].name, "ProcessId");
}

TEST(real_mojom_read_only_buffer_parses) {
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/read_only_buffer.mojom", {}, ReadRealMojomFile);
  EXPECT(graph[0].module.structs[0].fields[0].type.kind == TypeKind::kArray);
}

TEST(real_mojom_generic_pending_associated_receiver_parses) {
  LoadedGraph graph = LoadModuleGraph(
      "mojo/public/mojom/base/generic_pending_associated_receiver.mojom", {},
      ReadRealMojomFile);
  const Module& m = graph[0].module;
  EXPECT_EQ(m.structs.size(), 1u);
  EXPECT(m.structs[0].fields[1].type.kind ==
         TypeKind::kPendingAssociatedReceiver);
  EXPECT_EQ(m.interfaces.size(), 1u);
  EXPECT_EQ(m.interfaces[0].name, "GenericAssociatedInterface");
}

TEST(real_mojom_file_path_enable_if_selects_string_arm) {
  LoadedGraph dropped = LoadModuleGraph(
      "mojo/public/mojom/base/file_path.mojom", {}, ReadRealMojomFile);
  EXPECT_EQ(dropped[0].module.structs[0].fields.size(), 0u);

  LoadedGraph kept = LoadModuleGraph(
      "mojo/public/mojom/base/file_path.mojom", {}, ReadRealMojomFile,
      {"file_path_is_string"});
  EXPECT_EQ(kept[0].module.structs.size(), 2u);
  EXPECT_EQ(kept[0].module.structs[0].fields.size(), 1u);
  EXPECT(kept[0].module.structs[0].fields[0].type.kind == TypeKind::kString);
  EXPECT_EQ(kept[0].module.structs[1].fields.size(), 1u);
}
