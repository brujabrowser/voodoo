#ifndef WHP_BASE_TIME_H_
#define WHP_BASE_TIME_H_

#include <cstdint>

namespace whp {

class TimeDelta {
 public:
  static TimeDelta FromMicroseconds(int64_t us) { return TimeDelta(us); }
  static TimeDelta FromMilliseconds(int64_t ms) {
    return TimeDelta(ms * 1000);
  }
  static TimeDelta FromSeconds(int64_t s) { return TimeDelta(s * 1000000); }
  static TimeDelta Max() { return TimeDelta(kMaxUs); }

  int64_t InMicroseconds() const { return us_; }
  int64_t InMilliseconds() const { return us_ / 1000; }
  bool is_max() const { return us_ >= kMaxUs; }

  bool operator<=(TimeDelta o) const { return us_ <= o.us_; }
  bool operator<(TimeDelta o) const { return us_ < o.us_; }

 private:
  static constexpr int64_t kMaxUs = 0x7fffffffffffffffLL / 4;
  explicit TimeDelta(int64_t us) : us_(us) {}
  int64_t us_ = 0;
};

class TimeTicks {
 public:
  TimeTicks() = default;
  static TimeTicks Now();
  static TimeTicks UnixEpoch();

  int64_t InMicroseconds() const { return us_; }

  TimeDelta operator-(TimeTicks other) const {
    return TimeDelta::FromMicroseconds(us_ - other.us_);
  }
  TimeTicks operator+(TimeDelta d) const {
    return TimeTicks(us_ + d.InMicroseconds());
  }
  bool operator>=(TimeTicks other) const { return us_ >= other.us_; }

 private:
  explicit TimeTicks(int64_t us) : us_(us) {}
  int64_t us_ = 0;
};

}  // namespace whp

#endif  // WHP_BASE_TIME_H_
