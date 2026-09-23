#include "whp/base/time.h"

#include <chrono>

namespace whp {

TimeTicks TimeTicks::Now() {
  using namespace std::chrono;
  const auto us =
      duration_cast<microseconds>(steady_clock::now().time_since_epoch())
          .count();
  return TimeTicks(us);
}

TimeTicks TimeTicks::UnixEpoch() {
  using namespace std::chrono;
  const auto us =
      duration_cast<microseconds>(system_clock::now().time_since_epoch())
          .count();
  return TimeTicks(us);
}

}  // namespace whp
