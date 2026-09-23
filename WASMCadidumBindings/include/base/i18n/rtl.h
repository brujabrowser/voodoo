// Bindings is the base:: rung. Chromium maps mojo_base.mojom.TextDirection
// onto base::i18n::TextDirection (base/i18n/rtl.h). Same enumerators.
#ifndef BASE_I18N_RTL_H_
#define BASE_I18N_RTL_H_

namespace base {
namespace i18n {

enum TextDirection {
  UNKNOWN_DIRECTION = 0,
  RIGHT_TO_LEFT = 1,
  LEFT_TO_RIGHT = 2,
};

}  // namespace i18n
}  // namespace base

#endif  // BASE_I18N_RTL_H_
