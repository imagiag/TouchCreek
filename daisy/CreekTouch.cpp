// Creek — procedural stereo creek synthesis for the Synthux Touch 2 (Daisy Seed).
// Flow bed + bubble pool after van den Doel, "Physically-based Models for
// Liquid Sounds" (2005). The engine (creek/) is hardware-independent; this
// file and ui/ are the Touch 2 glue.
#include "daisy_seed.h"
#include "touch/touch.h"
#include "creek/creek_engine.h"
#include "ui/creek_ui.h"

// USB device names (Synthux's libDaisy fork expects these); the USB port shows up as "Creek"
const char* USBD_MANUFACTURER_STRING = "Synthux";
const char* USBD_PRODUCT_STRING_HS = "Creek";
const char* USBD_PRODUCT_STRING_FS = "Creek";

using namespace daisy;
using namespace synthux;

static constexpr uint32_t kSeed = 1;   // fixed: the same creek every power-up (as the reference renders)

DaisySeed hw;
Touch touch;
creek::CreekEngine engine;
CreekUI ui(touch, engine);

#if CREEK_MONITOR
CpuLoadMeter cpu;
#endif

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
#if CREEK_MONITOR
    cpu.OnBlockStart();
#endif
    ui.AudioBlock(out[0], out[1], size);
#if CREEK_MONITOR
    cpu.OnBlockEnd();
#endif
}

// Stack margin: the stack has the 128 KB DTCM to itself (nothing else is placed
// there). Fill the unused part with a pattern at boot; the untouched words
// left later show how deep the stack has ever reached.
static constexpr uint32_t kStackPattern = 0xC0FFEE11u;
static uint32_t* const kDtcmBase = (uint32_t*)0x20000000;
static uint32_t* const kDtcmTop = (uint32_t*)0x20020000;
#if CREEK_MONITOR
static void PaintStack() {
    uint32_t* sp;
    asm volatile("mov %0, sp" : "=r"(sp));
    for (uint32_t* p = kDtcmBase; p < sp - 64; p++) *p = kStackPattern;
}
static uint32_t StackUsedBytes() {
    uint32_t* p = kDtcmBase;
    while (p < kDtcmTop && *p == kStackPattern) p++;
    return (uint32_t)((uint8_t*)kDtcmTop - (uint8_t*)p);
}
#endif

int main(void) {
#if CREEK_MONITOR
    PaintStack();
#endif
    hw.Init(true);                     // boost to 480 MHz
    hw.SetAudioBlockSize(48);          // 1 ms blocks
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

#if CREEK_MONITOR
    // USB serial stats, once a second (see web/monitor.html). Does not wait for a computer.
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
#if CREEK_MONITOR
    uint32_t lastLog = lastUi;
    float worst = 0;
    creek::Stats prev; engine.GetStats(prev);
#endif
    while (1) {
        ui.PollMidi();                       // every pass: low MIDI latency
        const uint32_t ms = System::GetNow();
        if (ms - lastUi >= 4) { lastUi = ms; ui.Process(hw); }
#if CREEK_MONITOR
        if (ms - lastLog >= 1000) {
            lastLog = ms;
            const float mx = cpu.GetMaxCpuLoad();
            if (mx > worst) worst = mx;
            creek::Stats s; engine.GetStats(s);
            // one line per second: easy to read in a serial monitor and to parse
            // CPU in tenths of a percent, printed as integers (no float printf)
            const int avg10 = (int)(cpu.GetAvgCpuLoad() * 1000.f), max10 = (int)(mx * 1000.f), worst10 = (int)(worst * 1000.f);
            // Short keys: libDaisy's logger buffer is 128 bytes per line.
            // c cpu avg, m max this second, w worst since boot (tenths of %), v voices, p peak voices,
            // b bubbles/s, s steals/s, d dropped/s, f splashes/s, t surface ticks/s, q queue drops (total),
            // l limited samples/s, k stack bytes, x stress test on
            hw.PrintLine("CREEK c %d.%d m %d.%d w %d.%d v %d p %d b %lu s %lu d %lu f %lu t %lu q %lu l %lu k %lu x %d",
                         avg10 / 10, avg10 % 10, max10 / 10, max10 % 10, worst10 / 10, worst10 % 10,
                         s.active, s.maxActive,
                         (unsigned long)(s.births - prev.births), (unsigned long)(s.steals - prev.steals), (unsigned long)(s.drops - prev.drops),
                         (unsigned long)(s.splashes - prev.splashes), (unsigned long)(s.surface - prev.surface),
                         (unsigned long)(s.queueDrops + ui.QueueDrops()), (unsigned long)(s.limited - prev.limited),
                         (unsigned long)StackUsedBytes(), ui.Stress() ? 1 : 0);
            prev = s;
            cpu.Reset();
        }
#endif
    }
}
