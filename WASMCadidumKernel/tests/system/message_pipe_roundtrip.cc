#include "test.h"

#include "mojo/public/cpp/system/message_pipe.h"

#include <string>
#include <utility>
#include <vector>

TEST(CreateAndClose) {
  mojo::ScopedMessagePipeHandle a, b;
  EXPECT_EQ(mojo::CreateMessagePipe(nullptr, &a, &b), MOJO_RESULT_OK);
  EXPECT(a.is_valid());
  EXPECT(b.is_valid());
}

TEST(FuseMessagePipesSplices) {
  mojo::ScopedMessagePipeHandle a0, a1, b0, b1;
  EXPECT_EQ(mojo::CreateMessagePipe(nullptr, &a0, &a1), MOJO_RESULT_OK);
  EXPECT_EQ(mojo::CreateMessagePipe(nullptr, &b0, &b1), MOJO_RESULT_OK);
  EXPECT_EQ(mojo::FuseMessagePipes(std::move(a1), std::move(b1)),
            MOJO_RESULT_OK);
  const char* hi = "hi";
  EXPECT_EQ(mojo::WriteMessageRaw(a0.get(), hi, 2, nullptr, 0, 0),
            MOJO_RESULT_OK);
  v8::internal::CageBytes payload;
  std::vector<MojoHandle> handles;
  EXPECT_EQ(mojo::ReadMessageRaw(b0.get(), &payload, &handles, 0),
            MOJO_RESULT_OK);
  EXPECT_EQ(payload.size(), 2u);
}

TEST(WriteReadRaw) {
  mojo::ScopedMessagePipeHandle a, b;
  EXPECT_EQ(mojo::CreateMessagePipe(nullptr, &a, &b), MOJO_RESULT_OK);

  const std::string hello = "hello kernel";
  EXPECT_EQ(mojo::WriteMessageRaw(a.get(), hello.data(),
                                  static_cast<uint32_t>(hello.size()),
                                  nullptr, 0, MOJO_WRITE_MESSAGE_FLAG_NONE),
            MOJO_RESULT_OK);

  v8::internal::CageBytes payload;
  std::vector<MojoHandle> handles;
  EXPECT_EQ(mojo::ReadMessageRaw(b.get(), &payload, &handles,
                                 MOJO_READ_MESSAGE_FLAG_NONE),
            MOJO_RESULT_OK);
  EXPECT_EQ(payload.size(), hello.size());
  EXPECT(std::string(payload.begin(), payload.end()) == hello);
  EXPECT(handles.empty());
}

TEST(MoveOnlyRelease) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);
  mojo::ScopedMessagePipeHandle moved = std::move(a);
  EXPECT(!a.is_valid());
  EXPECT(moved.is_valid());
}
