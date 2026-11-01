#include "Euc.h"
#include "Protocols.h"

namespace euc {

std::unique_ptr<Protocol> makeProtocol(const Brand brand) {
  switch (brand) {
    case Brand::Begode:
      return makeBegode();
    case Brand::Veteran:
      return makeVeteran();
    case Brand::Kingsong:
      return makeKingsong();
    case Brand::InmotionV1:
      return makeInmotionV1();
    case Brand::InmotionV2:
      return makeInmotionV2();
    case Brand::NinebotS:
      return makeNinebotS();
    case Brand::NinebotE:
      return makeNinebotE();
    case Brand::NinebotZ:
      return makeNinebotZ();
    case Brand::Vesc:
      return makeVesc();
    case Brand::Count:
      break;
  }
  return nullptr;
}

std::unique_ptr<WheelSimulator> makeSimulator(const Brand brand) {
  switch (brand) {
    case Brand::Begode:
      return makeBegodeSim();
    case Brand::Veteran:
      return makeVeteranSim();
    case Brand::Kingsong:
      return makeKingsongSim();
    case Brand::InmotionV1:
      return makeInmotionV1Sim();
    case Brand::InmotionV2:
      return makeInmotionV2Sim();
    case Brand::NinebotS:
      return makeNinebotSSim();
    case Brand::NinebotE:
      return makeNinebotESim();
    case Brand::NinebotZ:
      return makeNinebotZSim();
    case Brand::Vesc:
      return makeVescSim();
    case Brand::Count:
      break;
  }
  return nullptr;
}

const char* brandName(const Brand brand) {
  switch (brand) {
    case Brand::Begode:
      return "Begode";
    case Brand::Veteran:
      return "Veteran / Leaperkim";
    case Brand::Kingsong:
      return "KingSong";
    case Brand::InmotionV1:
      return "InMotion V5/V8/V10";
    case Brand::InmotionV2:
      return "InMotion V9-V14, P6";
    case Brand::NinebotS:
      return "Ninebot One S/A1";
    case Brand::NinebotE:
      return "Ninebot One E+/C/P";
    case Brand::NinebotZ:
      return "Ninebot Z";
    case Brand::Vesc:
      return "VESC";
    case Brand::Count:
      break;
  }
  return "";
}

}  // namespace euc
