// Scenario files (scenarios/*.txt) for the C++ host tools: same format and
// semantics as web/engine/creek-scenario.js (see there for the syntax).
#pragma once
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "../creek/creek_engine.h"
#include "../creek/controls.h"

namespace scenario {

constexpr int kRampStep = 48;

struct Point { long s; int order; std::string op, name; std::vector<std::pair<std::string, float>> v; float hz[4] = {0, 0, 0, 0}; };
struct Ramp { long s0, s1; std::string key; float v0, v1; };
struct Scenario { float seconds = 10; uint32_t seed = 1; std::vector<Point> points; std::vector<Ramp> ramps; std::string text; };

inline std::string readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open " + path);
    std::string s; char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

inline Scenario parse(const std::string& text, float sr) {
    static const char* keys[] = {"flow", "roughness", "scale", "aeration", "gain", "width", "mask", "hold", "freeflow", "variety", "decay", "scalemask", "tone", "tunedetail", "delaytime", "delay"};
    static const char* bkeys[] = {"zone", "radius", "amp", "tau", "rise", "pan"};
    auto has = [](const char* const* ks, int n, const std::string& k) { for (int i = 0; i < n; i++) if (k == ks[i]) return true; return false; };
    Scenario sc; sc.text = text;
    auto toS = [&](double t) { return (long)llround(t * sr); };
    int order = 0;
    size_t p = 0;
    while (p <= text.size()) {
        size_t e = text.find('\n', p); if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p); p = e + 1;
        const size_t h = line.find('#'); if (h != std::string::npos) line.resize(h);
        std::vector<std::string> w; { char* tmp = strdup(line.c_str()); for (char* t = strtok(tmp, " \t\r"); t; t = strtok(nullptr, " \t\r")) w.push_back(t); free(tmp); }
        if (w.empty()) continue;
        auto kv = [&](size_t from, const char* const* ks, int nk) {
            std::vector<std::pair<std::string, float>> o;
            for (size_t i = from; i + 1 < w.size(); i += 2) {
                if (!has(ks, nk, w[i])) throw std::runtime_error("unknown key " + w[i]);
                o.push_back({w[i], (float)atof(w[i + 1].c_str())});
            }
            return o;
        };
        if (w[0] == "seconds") sc.seconds = (float)atof(w[1].c_str());
        else if (w[0] == "seed") sc.seed = (uint32_t)strtoul(w[1].c_str(), nullptr, 10);
        else if (w[0] == "at") {
            Point pt; pt.s = toS(atof(w[1].c_str())); pt.order = order++; pt.op = w[2];
            if (pt.op == "preset") pt.name = w[3];
            else if (pt.op == "set") pt.v = kv(3, keys, (int)(sizeof keys / sizeof *keys));
            else if (pt.op == "bubble") pt.v = kv(3, bkeys, 6);
            else if (pt.op == "stone") { static const char* sk[] = {"amp"}; pt.v = kv(3, sk, 1); }
            else if (pt.op == "drop") { static const char* dk[] = {"hz", "amp", "pan"}; pt.v = kv(3, dk, 3); }
            else if (pt.op == "tune") { for (size_t i = 3; i < w.size() && i < 7; i++) pt.hz[i - 3] = (float)atof(w[i].c_str()); }
            else throw std::runtime_error("unknown op " + pt.op);
            sc.points.push_back(pt);
        } else if (w[0] == "every") {
            const double t0 = atof(w[1].c_str()), t1 = atof(w[2].c_str()), dt = atof(w[3].c_str());
            if (w[4] != "bubble") throw std::runtime_error("every: only bubble");
            auto v = kv(5, bkeys, 6);
            for (int k = 0; t0 + k * dt < t1 - 1e-9; k++) { Point pt; pt.s = toS(t0 + k * dt); pt.order = order++; pt.op = "bubble"; pt.v = v; sc.points.push_back(pt); }
        } else if (w[0] == "ramp") {
            if (!has(keys, (int)(sizeof keys / sizeof *keys), w[3])) throw std::runtime_error("unknown key " + w[3]);
            sc.ramps.push_back({toS(atof(w[1].c_str())), toS(atof(w[2].c_str())), w[3], (float)atof(w[4].c_str()), (float)atof(w[5].c_str())});
        } else throw std::runtime_error("unknown line: " + line);
    }
    std::stable_sort(sc.points.begin(), sc.points.end(), [](const Point& a, const Point& b) { return a.s < b.s || (a.s == b.s && a.order < b.order); });
    return sc;
}

struct M { float flow = 0.2f, roughness = 0.25f, scale = 0.2f, aeration = 0.3f, gain = 1, width = 1, mask = 63, hold = 0; float tuneHz[4] = {0, 0, 0, 0}; float freeflow = 1, variety = 0.5f, decay = 0.5f, scalemask = 4095, tone = 0.5f, tunedetail = 0, delaytime = 0.375f, delay = 0; };
inline void setKey(M& m, const std::string& k, float v) {
    if (k == "flow") m.flow = v; else if (k == "roughness") m.roughness = v; else if (k == "scale") m.scale = v;
    else if (k == "aeration") m.aeration = v; else if (k == "gain") m.gain = v; else if (k == "width") m.width = v;
    else if (k == "mask") m.mask = v; else if (k == "hold") m.hold = v;
    else if (k == "freeflow") m.freeflow = v; else if (k == "variety") m.variety = v; else if (k == "decay") m.decay = v;
    else if (k == "scalemask") m.scalemask = v; else if (k == "tone") m.tone = v; else if (k == "tunedetail") m.tunedetail = v;
    else if (k == "delaytime") m.delaytime = v; else if (k == "delay") m.delay = v;
}

// Renders into L/R (resized). Processing is split at automation points, so the
// result does not depend on `block`. onBlock(pos, n) is called after each chunk.
template <typename F>
void run(const Scenario& sc, creek::CreekEngine& eng, float sr, int block, std::vector<float>& L, std::vector<float>& R, F onBlock) {
    const long n = (long)floorf(sc.seconds * sr);
    L.assign(n, 0.f); R.assign(n, 0.f);
    M m;
    eng.Reset(sc.seed);
    size_t pi = 0; long pos = 0;
    while (pos < n) {
        bool changed = pos == 0;
        for (; pi < sc.points.size() && sc.points[pi].s <= pos; pi++) {
            const Point& p = sc.points[pi];
            if (p.op == "preset") {
                for (int i = 0; i < creek::kNumPresets; i++) if (p.name == creek::kPresets[i].key) {
                    m.flow = creek::kPresets[i].flow; m.roughness = creek::kPresets[i].roughness;
                    m.scale = creek::kPresets[i].scale; m.aeration = creek::kPresets[i].aeration;
                }
                changed = true;
            } else if (p.op == "set") { for (auto& kv : p.v) setKey(m, kv.first, kv.second); changed = true; }
            else if (p.op == "tune") { for (int k = 0; k < 4; k++) m.tuneHz[k] = p.hz[k]; changed = true; }
            else if (p.op == "stone") {
                creek::Event e; e.type = creek::EV_STONE; e.amp = 0.8f;
                for (auto& kv : p.v) if (kv.first == "amp") e.amp = kv.second;
                eng.Trigger(e);
            }
            else if (p.op == "drop") {
                creek::Event e; e.type = creek::EV_DROP; e.amp = 0.7f;
                for (auto& kv : p.v) {
                    if (kv.first == "hz") e.radiusMm = 3000.f / kv.second; else if (kv.first == "amp") e.amp = kv.second;
                    else if (kv.first == "pan") e.pan = kv.second;
                }
                eng.Trigger(e);
            }
            else {
                creek::Event e;
                for (auto& kv : p.v) {
                    if (kv.first == "zone") e.zone = (uint8_t)kv.second; else if (kv.first == "radius") e.radiusMm = kv.second;
                    else if (kv.first == "amp") e.amp = kv.second; else if (kv.first == "tau") e.tauSec = kv.second;
                    else if (kv.first == "rise") e.rise = kv.second; else if (kv.first == "pan") e.pan = kv.second;
                }
                eng.Trigger(e);
            }
        }
        bool rampOn = false;
        for (auto& r : sc.ramps) if (pos <= r.s1 + kRampStep) rampOn = true;
        if (pos % kRampStep == 0) for (auto& r : sc.ramps) if (pos >= r.s0 && pos <= r.s1 + kRampStep) {
            const float x = fminf(1.f, (float)(pos - r.s0) / (float)std::max(1L, r.s1 - r.s0));
            setKey(m, r.key, r.v0 + (r.v1 - r.v0) * x); changed = true;
        }
        if (changed) {
            creek::Macros cm;
            cm.flow = m.flow; cm.roughness = m.roughness; cm.scale = m.scale; cm.aeration = m.aeration;
            cm.gain = m.gain; cm.width = m.width; cm.mask = (uint32_t)m.mask; cm.hold = m.hold != 0;
            cm.freeFlow = m.freeflow; cm.variety = m.variety; cm.decay = m.decay; cm.scaleMask = (uint32_t)m.scalemask; cm.tone = m.tone; cm.tuneDetail = m.tunedetail != 0;
            cm.delayTime = m.delaytime; cm.delayAmt = m.delay;
            cm.tune = false;
            for (int k = 0; k < 4; k++) { cm.tuneHz[k] = m.tuneHz[k]; if (m.tuneHz[k] > 0) cm.tune = true; }
            eng.SetMacros(cm);
        }
        long next = rampOn ? std::min(n, (pos / kRampStep + 1) * kRampStep) : n;
        if (pi < sc.points.size() && sc.points[pi].s < next) next = sc.points[pi].s;
        const long k = std::min((long)block, next - pos);
        eng.Process(&L[pos], &R[pos], (size_t)k);
        onBlock(pos, k);
        pos += k;
    }
}

} // namespace scenario
