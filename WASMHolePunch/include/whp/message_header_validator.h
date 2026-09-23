#ifndef WHP_MESSAGE_HEADER_VALIDATOR_H_
#define WHP_MESSAGE_HEADER_VALIDATOR_H_

#include "whp/message.h"

#include <string>

namespace whp {

class MessageHeaderValidator : public MessageReceiver {
 public:
  MessageHeaderValidator();
  explicit MessageHeaderValidator(std::string description);

  void SetDescription(std::string description);

  bool Accept(Message* message) override;

 private:
  std::string description_;
};

}  // namespace whp

#endif  // WHP_MESSAGE_HEADER_VALIDATOR_H_
