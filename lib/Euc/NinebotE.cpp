#include "NinebotE.h"

#include "Protocols.h"

namespace euc::ninebot {

// eucNinebotE.js:47-49: one characteristic, FFE1, notifies and is written.
const GattProfile kProfileE{"0000ffe0-0000-1000-8000-00805f9b34fb",
                            "0000ffe1-0000-1000-8000-00805f9b34fb",
                            "0000ffe0-0000-1000-8000-00805f9b34fb",
                            "0000ffe1-0000-1000-8000-00805f9b34fb",
                            false,
                            ""};
const Variant kVariantE{Brand::NinebotE, &kProfileE, "Ninebot One E+/C/P", kAddrE};

}  // namespace euc::ninebot

namespace euc {

std::unique_ptr<Protocol> makeNinebotE() { return ninebot::makeRegisterProtocol(ninebot::kVariantE); }
std::unique_ptr<WheelSimulator> makeNinebotESim() { return ninebot::makeRegisterSim(ninebot::kVariantE); }

}  // namespace euc
