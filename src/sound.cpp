#include "sound.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace sfx {
namespace {

constexpr int kRate = 22050;
constexpr int kBufSamples = 512;  // ~23 ms per buffer
constexpr int kBufCount = 3;
constexpr double kPi = 3.14159265358979323846;

using Buf = std::vector<float>;
enum Wave { Square, Triangle };

float Shape(Wave w, double phase) {
    double f = phase - std::floor(phase);
    return w == Square ? (f < 0.5 ? 1.0f : -1.0f) : static_cast<float>(4.0 * std::fabs(f - 0.5) - 1.0);
}

// Short fades at both ends keep the sounds free of clicks.
void Ramp(Buf& b) {
    size_t r = std::min<size_t>(b.size() / 2, kRate / 400);
    for (size_t i = 0; i < r; ++i) {
        float k = static_cast<float>(i) / static_cast<float>(r);
        b[i] *= k;
        b[b.size() - 1 - i] *= k;
    }
}

Buf Sweep(double f0, double f1, double sec, Wave w, float vol, bool decay = true) {
    size_t n = static_cast<size_t>(sec * kRate);
    Buf b(n);
    double phase = 0;
    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(n);
        phase += (f0 + (f1 - f0) * t) / kRate;
        b[i] = Shape(w, phase) * vol * static_cast<float>(decay ? 1.0 - t : 1.0);
    }
    Ramp(b);
    return b;
}

Buf Wobble(double base, double depth, double rate, double sec, Wave w, float vol) {
    size_t n = static_cast<size_t>(sec * kRate);
    Buf b(n);
    double phase = 0;
    for (size_t i = 0; i < n; ++i) {
        double t = static_cast<double>(i) / kRate;
        phase += (base + depth * std::sin(2 * kPi * rate * t)) / kRate;
        b[i] = Shape(w, phase) * vol;
    }
    return b;
}

// Gritty arcade noise: a random level held for `hold` samples, fading out.
Buf Noise(double sec, float vol, int hold) {
    static std::mt19937 rng(7);
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    size_t n = static_cast<size_t>(sec * kRate);
    Buf b(n);
    float v = 0;
    for (size_t i = 0; i < n; ++i) {
        if (i % static_cast<size_t>(hold) == 0) v = d(rng);
        b[i] = v * vol * static_cast<float>(1.0 - static_cast<double>(i) / static_cast<double>(n));
    }
    Ramp(b);
    return b;
}

Buf Mix(Buf a, const Buf& b) {
    if (a.size() < b.size()) a.resize(b.size());
    for (size_t i = 0; i < b.size(); ++i) a[i] += b[i];
    return a;
}

void Append(Buf& a, const Buf& b) { a.insert(a.end(), b.begin(), b.end()); }

Buf Notes(std::initializer_list<std::pair<double, double>> notes, Wave w, float vol) {
    Buf b;
    for (auto [f, sec] : notes) Append(b, f > 0 ? Sweep(f, f, sec, w, vol, false) : Buf(static_cast<size_t>(sec * kRate)));
    return b;
}

std::vector<Buf> Build() {
    std::vector<Buf> s(Count);
    const double march[4] = {98, 87, 78, 73};
    for (int i = 0; i < 4; ++i) s[InvMarch0 + i] = Sweep(march[i], march[i] * 0.97, 0.08, Square, 0.32f);
    s[InvShot] = Mix(Sweep(1400, 260, 0.14, Square, 0.16f), Noise(0.05, 0.08f, 2));
    s[InvAlienHit] = Mix(Noise(0.2, 0.34f, 6), Sweep(420, 80, 0.2, Square, 0.12f));
    s[InvPlayerHit] = Mix(Noise(0.75, 0.38f, 10), Sweep(320, 40, 0.75, Square, 0.14f));
    s[InvUfo] = Wobble(640, 220, 8, 0.25, Square, 0.07f);  // one full wobble cycle per loop pass
    Buf ufoHit;
    for (int i = 0; i < 6; ++i) Append(ufoHit, Sweep(300 + i * 160, 300 + i * 160, 0.045, Square, 0.16f, false));
    s[InvUfoHit] = Mix(ufoHit, Noise(0.27, 0.12f, 4));
    s[InvWave] = Notes({{523, 0.08}, {659, 0.08}, {784, 0.08}, {1047, 0.2}}, Square, 0.13f);

    // An original little fanfare (not any arcade game's tune): two rising arpeggios and a top note.
    Buf melody = Notes({{392, 0.11}, {523, 0.11}, {659, 0.11}, {784, 0.22}, {0, 0.04}, {440, 0.11}, {587, 0.11},
                        {698, 0.11}, {880, 0.22}, {0, 0.04}, {784, 0.11}, {988, 0.11}, {1047, 0.36}},
                       Square, 0.12f);
    Buf bass = Notes({{131, 0.59}, {147, 0.59}, {196, 0.2}, {131, 0.38}}, Triangle, 0.22f);
    s[MazeStart] = Mix(melody, bass);
    s[MazeChompA] = Sweep(240, 660, 0.07, Triangle, 0.3f, false);
    s[MazeChompB] = Sweep(660, 240, 0.07, Triangle, 0.3f, false);
    s[MazeSiren] = Wobble(450, 70, 2, 0.5, Triangle, 0.07f);
    s[MazePower] = Sweep(170, 620, 0.16, Square, 0.06f, false);
    s[MazeEatGhost] = Sweep(150, 1800, 0.24, Square, 0.15f, false);
    s[MazeFruit] = Notes({{880, 0.06}, {1320, 0.06}, {1760, 0.09}}, Triangle, 0.25f);
    Buf death;
    for (int i = 0; i < 10; ++i) {
        double f0 = 900 - i * 70;
        Append(death, Sweep(f0, f0 * 0.55, 0.12, Square, 0.15f));
    }
    Append(death, Buf(kRate / 12));
    Append(death, Sweep(200, 800, 0.08, Square, 0.15f, false));
    Append(death, Buf(kRate / 30));
    Append(death, Sweep(200, 800, 0.08, Square, 0.15f, false));
    s[MazeDeath] = death;
    s[MazeLevel] = Notes({{659, 0.07}, {784, 0.07}, {988, 0.07}, {1319, 0.22}}, Square, 0.13f);
    return s;
}

struct Voice {
    int id;
    size_t pos;
    bool loop;
};

CRITICAL_SECTION g_lock;
std::vector<Buf> g_sounds;
std::vector<Voice> g_voices;
HWAVEOUT g_out;
WAVEHDR g_hdr[kBufCount];
int16_t g_pcm[kBufCount][kBufSamples];
HANDLE g_event, g_thread;
volatile bool g_stop;
bool g_ready, g_failed, g_muted, g_mutedLoaded;

void Fill(int k) {
    float mix[kBufSamples] = {};
    EnterCriticalSection(&g_lock);
    for (auto& v : g_voices) {
        const Buf& b = g_sounds[static_cast<size_t>(v.id)];
        for (int i = 0; i < kBufSamples && !b.empty(); ++i) {
            if (v.pos >= b.size()) {
                if (!v.loop) break;
                v.pos = 0;
            }
            mix[i] += b[v.pos++];
        }
    }
    g_voices.erase(std::remove_if(g_voices.begin(), g_voices.end(),
                                  [](const Voice& v) { return !v.loop && v.pos >= g_sounds[static_cast<size_t>(v.id)].size(); }),
                   g_voices.end());
    LeaveCriticalSection(&g_lock);
    for (int i = 0; i < kBufSamples; ++i)
        g_pcm[k][i] = static_cast<int16_t>(std::clamp(mix[i] * 0.8f, -1.0f, 1.0f) * 32000.0f);
}

DWORD WINAPI Pump(void*) {
    while (!g_stop) {
        WaitForSingleObject(g_event, 50);
        for (int k = 0; k < kBufCount; ++k)
            if (g_hdr[k].dwFlags & WHDR_DONE) {
                Fill(k);
                waveOutWrite(g_out, &g_hdr[k], sizeof(WAVEHDR));
            }
    }
    return 0;
}

bool Ready() {
    if (g_ready) return true;
    if (g_failed) return false;
    InitializeCriticalSection(&g_lock);
    g_sounds = Build();
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = kRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = kRate * 2;
    if (waveOutOpen(&g_out, WAVE_MAPPER, &fmt, reinterpret_cast<DWORD_PTR>(g_event), 0, CALLBACK_EVENT) !=
        MMSYSERR_NOERROR) {
        g_failed = true;  // no audio device: the games simply stay silent
        return false;
    }
    for (int k = 0; k < kBufCount; ++k) {
        g_hdr[k] = {};
        g_hdr[k].lpData = reinterpret_cast<LPSTR>(g_pcm[k]);
        g_hdr[k].dwBufferLength = sizeof(g_pcm[k]);
        waveOutPrepareHeader(g_out, &g_hdr[k], sizeof(WAVEHDR));
        Fill(k);
        waveOutWrite(g_out, &g_hdr[k], sizeof(WAVEHDR));
    }
    g_thread = CreateThread(nullptr, 0, Pump, nullptr, 0, nullptr);
    g_ready = true;
    return true;
}

void LoadMuted() {
    if (g_mutedLoaded) return;
    g_mutedLoaded = true;
    DWORD v = 0, size = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\ReaNotepad", L"GameSoundOff", RRF_RT_REG_DWORD, nullptr, &v,
                     &size) == ERROR_SUCCESS)
        g_muted = v != 0;
}

void Add(Id id, bool loop) {
    LoadMuted();
    if (g_muted || !Ready()) return;
    EnterCriticalSection(&g_lock);
    bool already = loop && std::any_of(g_voices.begin(), g_voices.end(), [&](const Voice& v) { return v.loop && v.id == id; });
    if (!already) g_voices.push_back({id, 0, loop});
    LeaveCriticalSection(&g_lock);
}

}  // namespace

void Play(Id id) { Add(id, false); }
void Loop(Id id) { Add(id, true); }

void StopLoop(Id id) {
    if (!g_ready) return;
    EnterCriticalSection(&g_lock);
    g_voices.erase(std::remove_if(g_voices.begin(), g_voices.end(), [&](const Voice& v) { return v.loop && v.id == id; }),
                   g_voices.end());
    LeaveCriticalSection(&g_lock);
}

void StopAll() {
    if (!g_ready) return;
    EnterCriticalSection(&g_lock);
    g_voices.clear();
    LeaveCriticalSection(&g_lock);
}

bool Muted() {
    LoadMuted();
    return g_muted;
}

void SetMuted(bool muted) {
    LoadMuted();
    g_muted = muted;
    if (muted) StopAll();
    DWORD v = muted ? 1 : 0;
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\ReaNotepad", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) ==
        ERROR_SUCCESS) {
        RegSetValueExW(key, L"GameSoundOff", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof v);
        RegCloseKey(key);
    }
}

void Shutdown() {
    if (!g_ready) return;
    g_stop = true;
    WaitForSingleObject(g_thread, 1000);
    CloseHandle(g_thread);
    waveOutReset(g_out);
    for (auto& h : g_hdr) waveOutUnprepareHeader(g_out, &h, sizeof(WAVEHDR));
    waveOutClose(g_out);
    CloseHandle(g_event);
    g_ready = false;
}

}  // namespace sfx
