#include "test.h"

#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "whp/base/executor.h"

TEST(AutomaticWatcherFiresOnWrite) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  int fired = 0;
  MojoResult last_result = MOJO_RESULT_UNKNOWN;
  mojo::SimpleWatcher watcher(mojo::SimpleWatcher::ArmingPolicy::kAutomatic);
  EXPECT_EQ(watcher.Watch(b.get(), MOJO_HANDLE_SIGNAL_READABLE,
                          [&](MojoResult result) {
                            ++fired;
                            last_result = result;
                            // Automatic policy re-arms immediately after
                            // this callback returns -- drain the message so
                            // READABLE clears and the watcher goes back to
                            // waiting instead of re-firing forever.
                            v8::internal::CageBytes payload;
                            std::vector<MojoHandle> handles;
                            mojo::ReadMessageRaw(b.get(), &payload, &handles,
                                                 MOJO_READ_MESSAGE_FLAG_NONE);
                          }),
            MOJO_RESULT_OK);

  EXPECT_EQ(fired, 0);
  mojo::WriteMessageRaw(a.get(), "x", 1, nullptr, 0,
                        MOJO_WRITE_MESSAGE_FLAG_NONE);

  whp::Executor::Current().RunUntilIdle();
  EXPECT_EQ(fired, 1);
  EXPECT_EQ(last_result, MOJO_RESULT_OK);
}

TEST(ManualWatcherRequiresArm) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  int fired = 0;
  mojo::SimpleWatcher watcher(mojo::SimpleWatcher::ArmingPolicy::kManual);
  watcher.Watch(b.get(), MOJO_HANDLE_SIGNAL_READABLE,
               [&](MojoResult) { ++fired; });

  mojo::WriteMessageRaw(a.get(), "x", 1, nullptr, 0,
                        MOJO_WRITE_MESSAGE_FLAG_NONE);
  whp::Executor::Current().RunUntilIdle();
  EXPECT_EQ(fired, 0);  // never armed -- no trigger was ever installed live

  MojoResult ready = MOJO_RESULT_UNKNOWN;
  EXPECT_EQ(watcher.Arm(&ready), MOJO_RESULT_FAILED_PRECONDITION);
  EXPECT_EQ(ready, MOJO_RESULT_OK);
  whp::Executor::Current().RunUntilIdle();
  EXPECT_EQ(fired, 1);
}

TEST(CancelDropsPendingNotification) {
  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  int fired = 0;
  auto watcher =
      std::make_unique<mojo::SimpleWatcher>(mojo::SimpleWatcher::ArmingPolicy::kAutomatic);
  watcher->Watch(b.get(), MOJO_HANDLE_SIGNAL_READABLE,
                [&](MojoResult) { ++fired; });

  mojo::WriteMessageRaw(a.get(), "x", 1, nullptr, 0,
                        MOJO_WRITE_MESSAGE_FLAG_NONE);
  watcher->Cancel();
  whp::Executor::Current().RunUntilIdle();
  EXPECT_EQ(fired, 0);
}
