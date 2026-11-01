#pragma once

#include <cstddef>
#include <cstdint>

#include "Euc.h"

// The wheel catalog: makers, models and how to tell a brand from what the wheel advertises. Sources: the protocol
// survey .work/euc-research.md section 4 (model to protocol), WheelDash `source/eucData.mc:107-190` (Veteran and
// Kingsong pack voltages), WheelLog `ref/InmotionAdapterV2.java:108-118` (InMotion V2 ids) and the WheelDash site
// name-prefix table `blkfribourg.github.io/_pages/guide-amazfit-settings.md:735-741`.
namespace euc {

struct Model {
  const char* maker;
  const char* name;
  Brand brand;
  uint16_t packNominalVolts;  // the pack's full-charge voltage as the community names it (84, 100.8 -> 101, 126 ...)
};

enum class Confidence : uint8_t {
  None,     // nothing matched
  Service,  // the advertised service alone (FFE0 is shared: ambiguous)
  Name,     // a name prefix matched (strong)
};

struct Detection {
  Confidence confidence = Confidence::None;
  Brand candidates[4] = {};  // best first
  uint8_t count = 0;         // 1 for a name match; up to 4 for an ambiguous FFE0 service
  Brand brand() const { return count > 0 ? candidates[0] : Brand::Count; }
  bool ambiguous() const { return count > 1; }
};

// 16-bit GATT services the brands advertise, as lower-case 128-bit strings (what the BLE scan hands over).
namespace uuid {
constexpr const char* kFfe0 = "0000ffe0-0000-1000-8000-00805f9b34fb";  // Begode, Veteran, Kingsong, Ninebot E
constexpr const char* kFff0 = "0000fff0-0000-1000-8000-00805f9b34fb";  // old Kingsong
constexpr const char* kFfe5 = "0000ffe5-0000-1000-8000-00805f9b34fb";  // InMotion V1 write service
constexpr const char* kNus = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";   // InMotion V2, Ninebot S/Z, VESC
}  // namespace uuid

namespace catalog {

const Model* models(size_t& count);
// Distinct makers in table order; returns how many were written (at most `cap`).
size_t makers(const char** out, size_t cap);
// Models of one maker (exact, case-sensitive maker name). Returns how many were written.
size_t modelsOf(const char* maker, const Model** out, size_t cap);
const Model* find(const char* maker, const char* name);

// Brand from the advertised name (prefix match, case-insensitive) and/or a 128-bit service UUID string (lower or
// upper case, nullptr when the scan saw none). A name match wins; FFE0 alone yields the four FFE0 brands.
Detection detectBrand(const char* advertisedName, const char* serviceUuidOrNull);

}  // namespace catalog

}  // namespace euc
