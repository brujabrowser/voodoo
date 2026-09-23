#include "test.h"

#include "mojo/public/cpp/system/invitation.h"
#include "mojo/public/cpp/system/message_pipe.h"

#include <string>

TEST(InvitationSendAcceptExtract) {
  mojo::LoopbackChannel send_channel(1234);
  mojo::OutgoingInvitation outgoing;
  mojo::ScopedMessagePipeHandle sender_pipe =
      outgoing.AttachMessagePipe("conn");
  EXPECT(sender_pipe.is_valid());

  EXPECT_EQ(mojo::OutgoingInvitation::Send(std::move(outgoing), send_channel),
            MOJO_RESULT_OK);

  mojo::LoopbackChannel accept_channel(1234);
  mojo::IncomingInvitation incoming =
      mojo::IncomingInvitation::Accept(accept_channel);
  EXPECT(incoming.is_valid());

  mojo::ScopedMessagePipeHandle receiver_pipe =
      incoming.ExtractMessagePipe("conn");
  EXPECT(receiver_pipe.is_valid());

  const std::string hi = "hi";
  EXPECT_EQ(mojo::WriteMessageRaw(sender_pipe.get(), hi.data(),
                                  static_cast<uint32_t>(hi.size()), nullptr,
                                  0, MOJO_WRITE_MESSAGE_FLAG_NONE),
            MOJO_RESULT_OK);

  v8::internal::CageBytes payload;
  std::vector<MojoHandle> handles;
  EXPECT_EQ(mojo::ReadMessageRaw(receiver_pipe.get(), &payload, &handles,
                                 MOJO_READ_MESSAGE_FLAG_NONE),
            MOJO_RESULT_OK);
  EXPECT(std::string(payload.begin(), payload.end()) == hi);
}
