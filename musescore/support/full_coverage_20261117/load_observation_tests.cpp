#include "vslloadobservation.h"
#include <iostream>
#include <cstdlib>
using namespace muse::vst;
int main()
{
    int checks = 0;
    auto check = [&](bool ok) { ++checks; if (!ok) { std::cerr << "FAILED " << checks; std::exit(1); } };
    VslLoadObservationGate gate;
    VslLoadObservation state { true, 1000, 1000000, 100000000 };
    gate.start(10000, state);
    check(gate.update(14999, state) == VslLoadObservationGate::Waiting);
    check(gate.update(15000, state) == VslLoadObservationGate::Quiet);
    gate.start(0, state);
    for (int second = 1; second <= 5; ++second) {
        state.readBytes += 209; state.readOperations += 2;
        check(gate.update(second * 1000, state) == (second < 5 ? VslLoadObservationGate::Waiting : VslLoadObservationGate::Quiet));
    }
    gate.start(0, state);
    state.readBytes += 8192;
    check(gate.update(4900, state) == VslLoadObservationGate::Waiting);
    check(gate.update(5000, state) == VslLoadObservationGate::Waiting);
    check(gate.update(9899, state) == VslLoadObservationGate::Waiting);
    check(gate.update(9900, state) == VslLoadObservationGate::Quiet);
    gate.start(0, state);
    state.readOperations += 64;
    check(gate.update(4900, state) == VslLoadObservationGate::Waiting);
    check(gate.update(5000, state) == VslLoadObservationGate::Waiting);
    gate.start(0, state);
    state.privateBytes -= 1500000;
    check(gate.update(2000, state) == VslLoadObservationGate::Waiting);
    state.privateBytes += 3000000;
    check(gate.update(4900, state) == VslLoadObservationGate::Waiting);
    check(gate.update(5000, state) == VslLoadObservationGate::Waiting);
    check(gate.update(9900, state) == VslLoadObservationGate::Quiet);
    gate.start(0, state);
    state.readBytes += 8192;
    check(gate.update(180000, state) == VslLoadObservationGate::Waiting);
    state.readBytes += 8192;
    check(gate.update(899999, state) == VslLoadObservationGate::Waiting);
    check(gate.update(900000, state) == VslLoadObservationGate::TimedOut);
    gate.start(0, state);
    state.readBytes += 8192;
    check(gate.update(250000, state) == VslLoadObservationGate::Waiting);
    check(gate.update(254999, state) == VslLoadObservationGate::Waiting);
    check(gate.update(255000, state) == VslLoadObservationGate::Quiet);
    gate.start(10000, state);
    state.readBytes += 8192;
    check(gate.update(909999, state) == VslLoadObservationGate::Waiting);
    check(gate.update(910000, state) == VslLoadObservationGate::TimedOut);
    state.valid = false;
    check(gate.update(1000, state) == VslLoadObservationGate::ObservationFailed);
    state.valid = true; gate.start(0, state); --state.readOperations;
    check(gate.update(1000, state) == VslLoadObservationGate::ObservationFailed);
    gate.start(1000, state);
    check(gate.update(999, state) == VslLoadObservationGate::ObservationFailed);
    std::cout << "PASS " << checks << " observation-policy checks (not a VSL ready acknowledgement)\n";
}
