// Compiles Chromium values.mojom (mutually recursive Value /
// DictionaryValue / ListValue) through voodoomc against live wcb.
// DictionaryValue/ListValue are CageMap/CageVector of incomplete Value.
#include "test.h"

#include "values_mojom_gen.h"

#include <utility>

TEST(real_mojom_values_generates_and_compiles_against_wcb) {
  mojo_base::mojom::Value leaf;
  leaf.set_string_value("ok");

  mojo_base::mojom::DictionaryValue dict;
  dict.storage.emplace("k", leaf);

  mojo_base::mojom::Value root;
  root.set_dictionary_value(std::move(dict));
  EXPECT(root.which() == mojo_base::mojom::Value::Tag::dictionary_value);
  EXPECT_EQ(root.dictionary_value().storage.at("k").string_value(), "ok");

  mojo_base::mojom::ListValue list;
  list.storage.push_back(leaf);
  mojo_base::mojom::Value arr;
  arr.set_list_value(std::move(list));
  EXPECT_EQ(arr.list_value().storage.size(), 1u);
}
