#pragma once

#include <memory>

#include "Euc.h"

// One factory pair per brand; lib/Euc/Factory.cpp (not part of the per-agent tests) dispatches makeProtocol and
// makeSimulator to these. Each brand's .cpp defines its two.
namespace euc {

std::unique_ptr<Protocol> makeBegode();
std::unique_ptr<WheelSimulator> makeBegodeSim();
std::unique_ptr<Protocol> makeVeteran();
std::unique_ptr<WheelSimulator> makeVeteranSim();
std::unique_ptr<Protocol> makeKingsong();
std::unique_ptr<WheelSimulator> makeKingsongSim();
std::unique_ptr<Protocol> makeInmotionV1();
std::unique_ptr<WheelSimulator> makeInmotionV1Sim();
std::unique_ptr<Protocol> makeInmotionV2();
std::unique_ptr<WheelSimulator> makeInmotionV2Sim();
std::unique_ptr<Protocol> makeNinebotS();
std::unique_ptr<WheelSimulator> makeNinebotSSim();
std::unique_ptr<Protocol> makeNinebotE();
std::unique_ptr<WheelSimulator> makeNinebotESim();
std::unique_ptr<Protocol> makeNinebotZ();
std::unique_ptr<WheelSimulator> makeNinebotZSim();
std::unique_ptr<Protocol> makeVesc();
std::unique_ptr<WheelSimulator> makeVescSim();

}  // namespace euc
