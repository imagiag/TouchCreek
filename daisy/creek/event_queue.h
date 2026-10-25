// EventQueue: pending events at absolute sample times (bounded, no allocation).
// Holds Trigger() events and, from Phase 2, splash children.
#pragma once
#include <stdint.h>
#include "config.h"

namespace creek {

enum EventType : uint8_t { EV_BUBBLE = 0, EV_SPLASH = 1, EV_DROP = 2, EV_STONE = 3 };

// Fields left at their "unset" value are drawn randomly from the zone's model.
struct Event {
    uint8_t type = EV_BUBBLE;
    uint8_t zone = 1;
    float radiusMm = 0;   // 0 = random
    float amp = 0;        // 0 = random
    float tauSec = 0;     // 0 = random
    float rise = -1;      // < 0 = random
    float pan = -2;       // < -1 = zone default + jitter
    float delaySec = 0;
    uint64_t at = 0;      // absolute sample (set by the engine)
};

class EventQueue {
public:
    Event ev[ck::QUEUE_SIZE];
    bool used[ck::QUEUE_SIZE];
    uint32_t drops = 0;

    void Reset() { for (int i = 0; i < ck::QUEUE_SIZE; i++) used[i] = false; drops = 0; }
    bool Push(const Event& e, uint64_t at) {
        for (int i = 0; i < ck::QUEUE_SIZE; i++) if (!used[i]) { ev[i] = e; ev[i].at = at; used[i] = true; return true; }
        drops++;
        return false;
    }
};

} // namespace creek
