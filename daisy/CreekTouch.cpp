// Creek — procedural stereo creek synthesis for the Synthux Touch 2 (Daisy Seed).
// Flow bed + bubble pool after van den Doel, "Physically-based Models for
// Liquid Sounds" (2005). The engine (creek/) is hardware-independent; this
// file and ui/ are the Touch 2 glue.
#include "daisy_seed.h"
#include "touch/touch.h"
#include "creek/creek_engine.h"
#include "ui/creek_ui.h"

using namespace daisy;
using namespace synthux;

static constexpr uint32_t kSeed = 1;   // fixed: the same creek every power-up (as the reference renders)

DaisySeed hw;
Touch touch;
creek::CreekEngine engine;
CreekUI ui(touch, engine);

#if DEBUG
CpuLoadMeter cpu;
#endif

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
#if DEBUG
    cpu.OnBlockStart();
#endif
    ui.AudioBlock(out[0], out[1], size);
#if DEBUG
    cpu.OnBlockEnd();
#endif
}

int main(void) {
    hw.Init(true);                     // boost to 480 MHz
    hw.SetAudioBlockSize(48);          // 1 ms blocks
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

#if DEBUG
    hw.StartLog(false);
    cpu.Init(hw.AudioSampleRate(), hw.AudioBlockSize());
#endif

    touch.Init(hw);
    for (int i = 0; i < 40; i++) { touch.Process(); System::Delay(3); } // let MPR121 baselines settle

    engine.Init(hw.AudioSampleRate(), kSeed);
    ui.Init(hw);
    ui.Process(hw);                    // read the panel once before sound starts
    hw.StartAudio(AudioCallback);

    uint32_t lastUi = System::GetNow();
#if DEBUG
    uint32_t lastLog = lastUi;
#endif
    while (1) {
        const uint32_t ms = System::GetNow();
        if (ms - lastUi >= 4) { lastUi = ms; ui.Process(hw); }
#if DEBUG
        if (ms - lastLog >= 1000) {
            lastLog = ms;
            creek::Stats s; engine.GetStats(s);
            hw.PrintLine("cpu avg " FLT_FMT3 "%% max " FLT_FMT3 "%% | voices %d max %d | births %lu steals %lu drops %lu | lim %lu",
                         FLT_VAR3(cpu.GetAvgCpuLoad() * 100.f), FLT_VAR3(cpu.GetMaxCpuLoad() * 100.f),
                         s.active, s.maxActive, (unsigned long)s.births, (unsigned long)s.steals,
                         (unsigned long)s.drops, (unsigned long)s.limited);
        }
#endif
    }
}
