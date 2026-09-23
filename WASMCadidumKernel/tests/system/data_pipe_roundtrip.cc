#include "test.h"

#include "mojo/public/cpp/system/data_pipe.h"

#include <cstring>
#include <string>

TEST(DataPipeWriteRead) {
  mojo::ScopedDataPipeProducerHandle producer;
  mojo::ScopedDataPipeConsumerHandle consumer;
  EXPECT_EQ(mojo::CreateDataPipe(nullptr, &producer, &consumer),
            MOJO_RESULT_OK);

  uint32_t n = 4;
  EXPECT_EQ(mojo::WriteDataRaw(producer.get(), "abcd", &n, nullptr),
            MOJO_RESULT_OK);
  EXPECT_EQ(n, 4u);

  char buf[8] = {};
  n = 4;
  EXPECT_EQ(mojo::ReadDataRaw(consumer.get(), buf, &n, nullptr),
            MOJO_RESULT_OK);
  EXPECT_EQ(n, 4u);
  EXPECT(std::string(buf, 4) == "abcd");
}

TEST(DataPipeBeginEnd) {
  mojo::ScopedDataPipeProducerHandle producer;
  mojo::ScopedDataPipeConsumerHandle consumer;
  mojo::CreateDataPipe(nullptr, &producer, &consumer);

  void* write_buf = nullptr;
  uint32_t write_n = 64;  // in/out: max bytes we're willing to claim
  EXPECT_EQ(mojo::BeginWriteDataRaw(producer.get(), nullptr, &write_buf,
                                    &write_n),
            MOJO_RESULT_OK);
  EXPECT(write_n >= 3u);
  std::memcpy(write_buf, "xyz", 3);
  EXPECT_EQ(mojo::EndWriteDataRaw(producer.get(), 3, nullptr), MOJO_RESULT_OK);

  const void* read_buf = nullptr;
  uint32_t read_n = 64;  // in/out: max bytes we're willing to consume
  EXPECT_EQ(mojo::BeginReadDataRaw(consumer.get(), nullptr, &read_buf,
                                   &read_n),
            MOJO_RESULT_OK);
  EXPECT_EQ(read_n, 3u);
  EXPECT(std::memcmp(read_buf, "xyz", 3) == 0);
  EXPECT_EQ(mojo::EndReadDataRaw(consumer.get(), 3, nullptr), MOJO_RESULT_OK);
}
