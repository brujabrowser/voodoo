#include "test.h"

#include "mojo/public/c/system/core.h"

#include <cstring>
#include <string>

TEST(InvitationLoopback) {
  MojoHandle invitation = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateInvitation(nullptr, &invitation), MOJO_RESULT_OK);
  EXPECT(invitation != MOJO_HANDLE_INVALID);

  MojoHandle sender_pipe = MOJO_HANDLE_INVALID;
  const char name[] = "default";
  EXPECT_EQ(MojoAttachMessagePipeToInvitation(
                invitation, name, sizeof(name) - 1, nullptr, &sender_pipe),
            MOJO_RESULT_OK);
  EXPECT(sender_pipe != MOJO_HANDLE_INVALID);

  MojoPlatformHandle platform_handle{};
  platform_handle.struct_size = sizeof(platform_handle);
  platform_handle.type = MOJO_PLATFORM_HANDLE_TYPE_INVALID;
  platform_handle.value = 42;  // loopback channel id convention
  MojoInvitationTransportEndpoint endpoint{};
  endpoint.struct_size = sizeof(endpoint);
  endpoint.type = MOJO_INVITATION_TRANSPORT_TYPE_CHANNEL;
  endpoint.num_platform_handles = 1;
  endpoint.platform_handles = &platform_handle;

  EXPECT_EQ(MojoSendInvitation(invitation, nullptr, &endpoint, nullptr, 0,
                               nullptr),
            MOJO_RESULT_OK);

  MojoHandle accepted = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoAcceptInvitation(&endpoint, nullptr, &accepted),
            MOJO_RESULT_OK);
  EXPECT(accepted != MOJO_HANDLE_INVALID);

  MojoHandle receiver_pipe = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoExtractMessagePipeFromInvitation(
                accepted, name, sizeof(name) - 1, nullptr, &receiver_pipe),
            MOJO_RESULT_OK);
  EXPECT(receiver_pipe != MOJO_HANDLE_INVALID);

  MojoMessageHandle msg = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoCreateMessage(nullptr, &msg), MOJO_RESULT_OK);
  MojoAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = MOJO_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  EXPECT_EQ(MojoAppendMessageData(msg, 2, nullptr, 0, &opts, &buf, &sz),
            MOJO_RESULT_OK);
  std::memcpy(buf, "hi", 2);
  EXPECT_EQ(MojoWriteMessage(sender_pipe, msg, nullptr), MOJO_RESULT_OK);

  MojoMessageHandle got = MOJO_MESSAGE_HANDLE_INVALID;
  EXPECT_EQ(MojoReadMessage(receiver_pipe, nullptr, &got), MOJO_RESULT_OK);
  void* rbuf = nullptr;
  uint32_t rn = 0;
  EXPECT_EQ(MojoGetMessageData(got, nullptr, &rbuf, &rn, nullptr, nullptr),
            MOJO_RESULT_OK);
  EXPECT_EQ(rn, 2u);
  EXPECT(std::string(static_cast<char*>(rbuf), 2) == "hi");

  EXPECT_EQ(MojoDestroyMessage(got), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(sender_pipe), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(receiver_pipe), MOJO_RESULT_OK);
  EXPECT_EQ(MojoClose(accepted), MOJO_RESULT_OK);
}

TEST(AcceptWithoutSendFails) {
  MojoPlatformHandle platform_handle{};
  platform_handle.struct_size = sizeof(platform_handle);
  platform_handle.value = 999999;  // never sent
  MojoInvitationTransportEndpoint endpoint{};
  endpoint.struct_size = sizeof(endpoint);
  endpoint.num_platform_handles = 1;
  endpoint.platform_handles = &platform_handle;

  MojoHandle accepted = MOJO_HANDLE_INVALID;
  EXPECT_EQ(MojoAcceptInvitation(&endpoint, nullptr, &accepted),
            MOJO_RESULT_NOT_FOUND);
}
