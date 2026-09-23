#include "test.h"

#include "mojo/public/cpp/system/buffer.h"

#include <cstring>

TEST(SharedBufferMapWriteReadBack) {
  mojo::ScopedSharedBufferHandle buffer;
  EXPECT_EQ(mojo::CreateSharedBuffer(64, nullptr, &buffer), MOJO_RESULT_OK);
  EXPECT(buffer.is_valid());
  MojoSharedBufferInfo info{};
  info.struct_size = sizeof(info);
  EXPECT_EQ(mojo::GetBufferInfo(buffer.get(), &info), MOJO_RESULT_OK);
  EXPECT_EQ(info.num_bytes, 64u);

  mojo::ScopedSharedBufferMapping mapping;
  EXPECT_EQ(mojo::MapBuffer(buffer.get(), 0, 64, &mapping), MOJO_RESULT_OK);
  EXPECT(mapping.is_valid());
  std::memcpy(mapping.get(), "kernel", 6);

  mojo::ScopedSharedBufferHandle dup;
  EXPECT_EQ(mojo::DuplicateBuffer(buffer.get(), nullptr, &dup),
            MOJO_RESULT_OK);
  EXPECT(dup.is_valid());

  mojo::ScopedSharedBufferMapping mapping2;
  EXPECT_EQ(mojo::MapBuffer(dup.get(), 0, 64, &mapping2), MOJO_RESULT_OK);
  EXPECT(std::memcmp(mapping2.get(), "kernel", 6) == 0);
}
