#include "Catalog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace euc {
namespace catalog {

namespace {
// Pack voltages: Veteran and Kingsong from WheelDash eucData.mc:107-190 (100.2/125.25 V full thresholds, the
// 84/100/126 V model lists); InMotion V11 84 V from eucData.mc:193. The rest are the voltages the makers publish;
// the reference repos do not tabulate them per model.
// UNVERIFIED: Begode, InMotion V12/V13/V14/P6/X1/E20, Ninebot and Floatwheel pack voltages are from public specs,
// not from a reference repo; they only seed the SoH series-count estimate and are never sent to a wheel.
constexpr std::array<Model, 50> kModels = {{
    {"Begode", "Mten3", Brand::Begode, 84},
    {"Begode", "Mten4", Brand::Begode, 84},
    {"Begode", "MCM5", Brand::Begode, 84},
    {"Begode", "T3", Brand::Begode, 84},
    {"Begode", "T4", Brand::Begode, 84},
    {"Begode", "Tesla", Brand::Begode, 84},
    {"Begode", "Nikola", Brand::Begode, 100},
    {"Begode", "MSuper Pro", Brand::Begode, 100},
    {"Begode", "MSP", Brand::Begode, 100},
    {"Begode", "RS", Brand::Begode, 100},
    {"Begode", "EX", Brand::Begode, 100},
    {"Begode", "EX20S", Brand::Begode, 100},
    {"Begode", "EXN", Brand::Begode, 100},
    {"Begode", "Monster", Brand::Begode, 100},
    {"Begode", "Monster Pro", Brand::Begode, 100},
    {"Begode", "Master", Brand::Begode, 134},
    {"Begode", "Master Pro", Brand::Begode, 134},
    {"Begode", "ET Max", Brand::Begode, 134},
    {"ExtremeBull", "Commander", Brand::Begode, 100},
    {"ExtremeBull", "Commander Pro", Brand::Begode, 134},
    {"Veteran", "Sherman", Brand::Veteran, 100},
    {"Veteran", "Sherman Max", Brand::Veteran, 100},
    {"Veteran", "Abrams", Brand::Veteran, 100},
    {"Veteran", "Sherman S", Brand::Veteran, 126},
    {"Veteran", "Patton", Brand::Veteran, 126},
    {"Veteran", "Lynx", Brand::Veteran, 151},
    {"Veteran", "Sherman L", Brand::Veteran, 151},
    {"Leaperkim", "Nosfet", Brand::Veteran, 126},
    {"Leaperkim", "Oryx", Brand::Veteran, 126},
    {"Kingsong", "KS-14D", Brand::Kingsong, 67},
    {"Kingsong", "KS-14S", Brand::Kingsong, 67},
    {"Kingsong", "KS-16S", Brand::Kingsong, 67},
    {"Kingsong", "KS-18L", Brand::Kingsong, 84},
    {"Kingsong", "KS-18XL", Brand::Kingsong, 84},
    {"Kingsong", "KS-16X", Brand::Kingsong, 84},
    {"Kingsong", "KS-S18", Brand::Kingsong, 84},
    {"Kingsong", "KS-S19", Brand::Kingsong, 100},
    {"Kingsong", "KS-S20", Brand::Kingsong, 126},
    {"Kingsong", "KS-S22", Brand::Kingsong, 126},
    {"InMotion", "V5", Brand::InmotionV1, 84},
    {"InMotion", "V8", Brand::InmotionV1, 84},
    {"InMotion", "V10", Brand::InmotionV1, 84},
    {"InMotion", "V11", Brand::InmotionV2, 84},
    {"InMotion", "V12", Brand::InmotionV2, 100},
    {"InMotion", "V13", Brand::InmotionV2, 126},
    {"InMotion", "V14", Brand::InmotionV2, 134},
    {"Ninebot", "One S2", Brand::NinebotS, 63},
    {"Ninebot", "One E+", Brand::NinebotE, 63},
    {"Ninebot", "One Z10", Brand::NinebotZ, 63},
    {"Floatwheel", "ADV", Brand::Vesc, 84},
}};

struct Prefix {
  const char* prefix;
  Brand brand;
};

// Name prefixes: site table SITE:737-740 (GW/GotWay, KS, LK/NF, V9..Adventure) plus the ones the open Garmin code
// checks: KSN (GarminEUCBleDelegate.mc:290-297), V11/V12 (:298-309), VESC (:310-318).
constexpr std::array<Prefix, 20> kPrefixes = {{
    {"GotWay", Brand::Begode},
    {"GW", Brand::Begode},
    {"KSN", Brand::Kingsong},
    {"KS", Brand::Kingsong},
    {"LK", Brand::Veteran},
    {"NF", Brand::Veteran},
    {"Adventure", Brand::InmotionV2},
    {"V11", Brand::InmotionV2},
    {"V12", Brand::InmotionV2},
    {"V13", Brand::InmotionV2},
    {"V14", Brand::InmotionV2},
    {"V9", Brand::InmotionV2},
    {"V6", Brand::InmotionV2},
    {"P6", Brand::InmotionV2},
    {"X1", Brand::InmotionV2},
    {"E20", Brand::InmotionV2},
    {"VESC", Brand::Vesc},
    // UNVERIFIED: eucWatch does not filter Ninebot by name; "Ninebot" is the Segway default name family.
    {"Ninebot", Brand::NinebotS},
    {"NBOne", Brand::NinebotS},
    {"Segway", Brand::NinebotS},
}};

bool startsWithNoCase(const char* s, const char* prefix) {
  for (; *prefix != '\0'; ++s, ++prefix) {
    if (*s == '\0' ||
        std::tolower(static_cast<unsigned char>(*s)) != std::tolower(static_cast<unsigned char>(*prefix))) {
      return false;
    }
  }
  return true;
}

bool sameNoCase(const char* a, const char* b) {
  for (; *a != '\0' && *b != '\0'; ++a, ++b) {
    if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
  }
  return *a == '\0' && *b == '\0';
}

// Accepts both the 128-bit string and a bare 16-bit "ffe0".
bool serviceIs(const char* uuid, const char* full) {
  if (uuid == nullptr) {
    return false;
  }
  if (sameNoCase(uuid, full)) {
    return true;
  }
  return std::strlen(uuid) == 4 && std::strncmp(full, "0000", 4) == 0 && startsWithNoCase(full + 4, uuid) &&
         std::strlen(full) > 8 && std::strncmp(full + 8, "-0000-1000-8000-00805f9b34fb", 28) == 0;
}
}  // namespace

const Model* models(size_t& count) {
  count = kModels.size();
  return kModels.data();
}

size_t makers(const char** out, const size_t cap) {
  size_t n = 0;
  for (const Model& m : kModels) {
    const bool seen = std::any_of(out, out + n, [&](const char* s) { return std::strcmp(s, m.maker) == 0; });
    if (!seen) {
      if (n >= cap) {
        break;
      }
      out[n++] = m.maker;
    }
  }
  return n;
}

size_t modelsOf(const char* maker, const Model** out, const size_t cap) {
  size_t n = 0;
  for (const Model& m : kModels) {
    if (n < cap && std::strcmp(m.maker, maker) == 0) {
      out[n++] = &m;
    }
  }
  return n;
}

const Model* find(const char* maker, const char* name) {
  const auto it = std::find_if(kModels.begin(), kModels.end(), [&](const Model& m) {
    return std::strcmp(m.maker, maker) == 0 && std::strcmp(m.name, name) == 0;
  });
  return it == kModels.end() ? nullptr : &*it;
}

Detection detectBrand(const char* advertisedName, const char* serviceUuidOrNull) {
  Detection d;
  if (advertisedName != nullptr && *advertisedName != '\0') {
    const auto it = std::find_if(kPrefixes.begin(), kPrefixes.end(),
                                 [&](const Prefix& p) { return startsWithNoCase(advertisedName, p.prefix); });
    if (it != kPrefixes.end()) {
      d.confidence = Confidence::Name;
      d.candidates[0] = it->brand;
      d.count = 1;
      // A Ninebot name on Nordic UART could be S or Z; both are listed so the user can pick.
      if (it->brand == Brand::NinebotS && serviceIs(serviceUuidOrNull, uuid::kNus)) {
        d.candidates[1] = Brand::NinebotZ;
        d.count = 2;
      } else if (it->brand == Brand::NinebotS && serviceIs(serviceUuidOrNull, uuid::kFfe0)) {
        d.candidates[0] = Brand::NinebotE;
      }
      return d;
    }
  }
  if (serviceIs(serviceUuidOrNull, uuid::kFfe0)) {
    d.confidence = Confidence::Service;
    d.candidates[0] = Brand::Begode;
    d.candidates[1] = Brand::Veteran;
    d.candidates[2] = Brand::Kingsong;
    d.candidates[3] = Brand::NinebotE;
    d.count = 4;
  } else if (serviceIs(serviceUuidOrNull, uuid::kFff0)) {
    d.confidence = Confidence::Service;
    d.candidates[0] = Brand::Kingsong;
    d.count = 1;
  } else if (serviceIs(serviceUuidOrNull, uuid::kFfe5)) {
    d.confidence = Confidence::Service;
    d.candidates[0] = Brand::InmotionV1;
    d.count = 1;
  } else if (serviceIs(serviceUuidOrNull, uuid::kNus)) {
    d.confidence = Confidence::Service;
    d.candidates[0] = Brand::InmotionV2;
    d.candidates[1] = Brand::Vesc;
    d.candidates[2] = Brand::NinebotS;
    d.candidates[3] = Brand::NinebotZ;
    d.count = 4;
  }
  return d;
}

}  // namespace catalog
}  // namespace euc
