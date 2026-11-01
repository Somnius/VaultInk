#include "NinebotS.h"

#include "Protocols.h"

namespace euc::ninebot {

// eucNinebotS.js:49-55.
const GattProfile kProfileS{"6e400001-b5a3-f393-e0a9-e50e24dcca9e",
                            "6e400003-b5a3-f393-e0a9-e50e24dcca9e",
                            "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
                            "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
                            false,
                            ""};
const Variant kVariantS{Brand::NinebotS, &kProfileS, "Ninebot One S/A1", kAddrS};

}  // namespace euc::ninebot

namespace euc {

std::unique_ptr<Protocol> makeNinebotS() { return ninebot::makeRegisterProtocol(ninebot::kVariantS); }
std::unique_ptr<WheelSimulator> makeNinebotSSim() { return ninebot::makeRegisterSim(ninebot::kVariantS); }

}  // namespace euc
