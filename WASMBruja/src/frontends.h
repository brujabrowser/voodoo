#ifndef BRUJA_FRONTENDS_H_
#define BRUJA_FRONTENDS_H_

#include <string>

namespace bruja {

enum class InputKind { kBruja, kMessagesIn, kCocoaMm };

InputKind InferInputKind(const std::string& path, const std::string& source);
std::string ToBrujaSource(InputKind kind, const std::string& source);

}  // namespace bruja

#endif  // BRUJA_FRONTENDS_H_
