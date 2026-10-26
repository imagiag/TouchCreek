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

#if !CREEK_MONITOR
// USB identity: libDaisy uses one product ID for every Daisy firmware, so iOS / Audiobus
// kept showing Creek under a name remembered from another firmware (PulsarTouch). Creek's
// MIDI build patches its own product ID and release number into the descriptor before
// USB starts, so hosts see a new device and read the name "Creek".
extern "C" uint8_t USBD_FS_DeviceDesc[];
extern "C" uint8_t USBD_HS_DeviceDesc[];
static constexpr uint16_t kCreekPid = 0x5743;   // Daisy default 0x5740
static constexpr uint16_t kCreekBcd = 0x0110;   // "1.10"
static void PatchUsbIdentity() {
    uint8_t* d[2] = {USBD_FS_DeviceDesc, USBD_HS_DeviceDesc};
    for (uint8_t* p : d) {
        p[10] = (uint8_t)(kCreekPid & 0xFF); p[11] = (uint8_t)(kCreekPid >> 8);
        p[12] = (uint8_t)(kCreekBcd & 0xFF); p[13] = (uint8_t)(kCreekBcd >> 8);
    }
}
#endif

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
#if !CREEK_MONITOR
    PatchUsbIdentity();                // before USB MIDI starts in ui.Init
#endif
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
        if (ms - lastUi >= 4) {
            lastUi = ms; ui.Process(hw);
#if CREEK_MONITOR
            CreekUI::Act x;                   // one event line per UI tick: what was pressed / moved
            if (ui.PopAct(x)) hw.PrintLine("CREEKE %c %d %d %d %d", x.type, x.a, x.b, x.c, x.d);
#endif
        }
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
            // second line: which layers are sounding (voices per layer, eddy / gate / activity in %)
            hw.PrintLine("CREEKL a %d u %d f %d t %d e %d g %d y %d h %d o %d m %lu n %lu",
                         s.active - s.activeUser, s.activeUser, s.activeFalls, s.activeSurface,
                         (int)(s.eddy * 100.f), (int)(s.gate * 100.f), (int)(s.activity * 100.f),
                         ui.HeldNow(), ui.ModeNow(), (unsigned long)ui.MaskNow(), (unsigned long)s.stones);
            prev = s;
            cpu.Reset();
        }
#endif
    }
}
