// KI-X360: the Xbox 360 frontend of this UltraCombo port. Replaces main.c,
// frontend.c and video.c (SDL2/OpenGL) with Direct3D 9, XAudio2 and XInput.
//
// Files go beside the .xex (game:\), in MAME's layout:
//   kinst.zip            boot and sound ROMs, unpacked once into cache\roms\kinst\
//   kinst\kinst.chd      the disk (or kinst.raw beside the .xex)
// Settings and high scores are kept in saves\, as on the PC.
//
// Controls (both pads): D-pad / left stick move; X Y RB = quick, medium,
// fierce punch; A B RT = quick, medium, fierce kick; Start = start;
// Back = coin; Back + LB + RB = test mode; Back + Start held 2 s = quit;
// left stick click = show the speed (also logged every 5 s); right stick
// click = settings menu (picture: 4:3 / blurred sides / 16:9 zoom, sharp,
// smooth or CRT, glow, brightness, saturation), saved in saves\kix360.ini.
#include <xtl.h>
#include <xaudio2.h>
#include <d3dx9.h>
#include <stdio.h>
#include <string.h>

#include "boot.h"
#include "machine.h"
#include "cpu.h"
#include "game.h"
#include "ide.h"
#include "launch.h"
#include "mem.h"
#include "osd.h"
#include "rec.h"
#include "sound.h"
#include "state.h"

// Cabinet buttons, in the bit order of the input ports (config.h on the PC).
enum { BTN_HQ, BTN_HM, BTN_HF, BTN_LQ, BTN_LM, BTN_LF,
       BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_START, BTN_COIN };
#include "padmap.h"

/* ---------------------------------------------------------------- */
/* Video                                                            */
/* ---------------------------------------------------------------- */

static IDirect3DDevice9 *dev;
static IDirect3DTexture9 *tex[2];
static IDirect3DVertexShader9 *vs;
static IDirect3DPixelShader9 *ps, *ps_bg;
static IDirect3DVertexDeclaration9 *decl;
static int screen_w, screen_h;
static float display_hz = 60.0f;
static uint32_t px[320 * 240];

// Picture options (the settings menu, saved in saves\kix360.ini).
// 4:3; 4:3 over a blurred fill; 16:9 stretched, all of it on screen; 16:9 cropped
enum { VIEW_43, VIEW_43_SIDES, VIEW_WIDE, VIEW_ZOOM, VIEW_N };
enum { FILTER_SHARP, FILTER_SMOOTH, FILTER_CRT, FILTER_N };
static struct {
    int view, filter, scanlines, glow, bright, sat;          // scanlines/glow 0-10, bright/sat in %
    int gamma;                                               // in hundredths: above 100 lifts the mid tones
    bool show_speed;
    int sound;                                               // in %: the board's output is quiet without a cabinet's amplifier
} opt = { VIEW_43, FILTER_SHARP, 6, 0, 100, 100, 115, false, 200 };   // TVs show the console's output darker than a PC monitor

static const char vs_src[] =
    "struct V { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
    "V main(float2 pos : POSITION, float2 uv : TEXCOORD0) { V o; o.pos = float4(pos, 0, 1); o.uv = uv; return o; }\n";

// The game picture. c0: texture size, 1/size. c1: filter (0 sharp, 1 smooth,
// 2 CRT), scanline strength, screen pixels per game pixel down, glow. c2:
// brightness, saturation, gamma, screen pixels per game pixel across.
// Sharp: bilinear only across the edge of each game pixel, so pixels stay
// square at any size. CRT: the same across, a soft bright line per game line
// down, and a faint phosphor mask. Glow: bright areas bleed light around
// them, like the tube's own bloom.
static const char ps_src[] =
    "sampler s : register(s0);\n"
    "float4 c0 : register(c0);\n"
    "float4 c1 : register(c1);\n"
    "float4 c2 : register(c2);\n"
    "float2 sharp(float2 uv, float2 k) {\n"
    "    float2 t = uv * c0.xy, f = frac(t) - 0.5, r = 0.5 - 0.5 / k;\n"
    "    f = (f - clamp(f, -r, r)) * k + 0.5;\n"
    "    return (floor(t) + f) * c0.zw;\n"
    "}\n"
    "float4 main(float2 uv : TEXCOORD0, float2 vp : VPOS) : COLOR {\n"
    "    float2 k = max(float2(c2.w, c1.z), float2(1.0, 1.0));\n"
    "    float3 c;\n"
    "    if (c1.x < 0.5) c = tex2D(s, sharp(uv, k)).rgb;\n"
    "    else if (c1.x < 1.5) c = tex2D(s, uv).rgb;\n"
    "    else {\n"
    "        float2 t = uv * c0.xy;\n"
    "        float2 a = float2(sharp(uv, k).x, (floor(t.y) + 0.5) * c0.w);\n"
    "        c = tex2D(s, a).rgb;\n"
    "        float d = frac(t.y) - 0.5;\n"
    "        float w = exp(-d * d * (2.0 + 22.0 * c1.y));\n"
    "        c *= lerp(1.0, w * (1.0 + 0.6 * c1.y), c1.y);\n"
    "        float m = fmod(vp.x, 3.0);\n"
    "        float3 mask = m < 1.0 ? float3(1, 0.8, 0.8) : (m < 2.0 ? float3(0.8, 1, 0.8) : float3(0.8, 0.8, 1));\n"
    "        c *= lerp(float3(1, 1, 1), mask * 1.15, 0.5 * c1.y);\n"
    "    }\n"
    "    if (c1.w > 0.0) {\n"
    "        float3 g = 0;\n"
    "        float2 o = 1.5 * c0.zw;\n"
    "        g += tex2D(s, uv + float2(o.x, 0)).rgb + tex2D(s, uv - float2(o.x, 0)).rgb;\n"
    "        g += tex2D(s, uv + float2(0, o.y)).rgb + tex2D(s, uv - float2(0, o.y)).rgb;\n"
    "        g += tex2D(s, uv + o).rgb + tex2D(s, uv - o).rgb;\n"
    "        g += tex2D(s, uv + float2(o.x, -o.y)).rgb + tex2D(s, uv + float2(-o.x, o.y)).rgb;\n"
    "        g = max(g / 8.0 - 0.45, 0.0) * 1.8;\n"
    "        c += g * c1.w;\n"
    "    }\n"
    "    float l = dot(c, float3(0.299, 0.587, 0.114));\n"
    "    c = lerp(float3(l, l, l), c, c2.y) * c2.x;\n"
    "    c = pow(saturate(c), c2.z);\n"
    "    return float4(saturate(c), 1);\n"
    "}\n";

// The fill beside a 4:3 picture: the picture itself, blurred and dimmed.
static const char ps_bg_src[] =
    "sampler s : register(s0);\n"
    "float4 c0 : register(c0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "    float3 c = 0;\n"
    "    for (int y = -3; y <= 3; y++)\n"
    "        for (int x = -3; x <= 3; x++)\n"
    "            c += tex2D(s, uv + float2(x, y) * 6.0 * c0.zw).rgb;\n"
    "    return float4(c / 49.0 * 0.45, 1);\n"
    "}\n";

static bool compile(const char *src, const char *profile, ID3DXBuffer **code)
{
    ID3DXBuffer *err = NULL;
    HRESULT hr = D3DXCompileShader(src, (UINT)strlen(src), NULL, NULL, "main", profile, 0, code, &err, NULL);
    if (err) { OutputDebugStringA((const char *)err->GetBufferPointer()); err->Release(); }
    return SUCCEEDED(hr);
}

static bool video_init(void)
{
    XVIDEO_MODE mode;
    memset(&mode, 0, sizeof mode);
    XGetVideoMode(&mode);
    // 720p on HD sets (the console scales to 1080 when the dashboard asks
    // for it), 480 lines otherwise; widescreen or 4:3 as the dashboard says.
    screen_w = mode.fIsHiDef ? 1280 : 640;
    screen_h = mode.fIsHiDef ? 720 : 480;
    if (!mode.fIsHiDef && mode.fIsWideScreen) screen_w = 854;
    if (mode.RefreshRate > 0) display_hz = mode.RefreshRate;

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return false;
    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = screen_w;
    pp.BackBufferHeight = screen_h;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (FAILED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev))) return false;
    // A frame that just misses the vertical blank is shown at once (a tear
    // near the top) instead of waiting a whole extra refresh.
    dev->SetRenderState(D3DRS_PRESENTIMMEDIATETHRESHOLD, 25);

    for (int i = 0; i < 2; i++)
        if (FAILED(dev->CreateTexture(320, 240, 1, 0, D3DFMT_LIN_X8R8G8B8, 0, &tex[i], NULL))) return false;

    ID3DXBuffer *code;
    if (!compile(vs_src, "vs_2_0", &code)) return false;
    dev->CreateVertexShader((const DWORD *)code->GetBufferPointer(), &vs);
    code->Release();
    if (!compile(ps_src, "ps_3_0", &code)) return false;
    dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &ps);
    code->Release();
    if (!compile(ps_bg_src, "ps_3_0", &code)) return false;
    dev->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &ps_bg);
    code->Release();
    static const D3DVERTEXELEMENT9 el[] = {
        { 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    dev->CreateVertexDeclaration(el, &decl);
    return vs && ps && ps_bg && decl;
}

static void draw_quad(float w, float h)       // half extents in clip space, centred
{
    const float quad[4][4] = {
        { -w,  h, 0, 0 }, { w,  h, 1, 0 },
        { -w, -h, 0, 1 }, { w, -h, 1, 1 },
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof quad[0]);
}

// Show a 320x240 0x00RRGGBB image filling the screen's height at 4:3, the
// whole screen (stretched across), or its width (cropping the top and bottom),
// with the chosen filter. Text screens
// (menus, errors) pass raw = true: plain sharp pixels, no effects.
static void present_ex(const uint32_t *img, bool raw)
{
    static int cur;
    IDirect3DTexture9 *t = tex[cur ^= 1];      // two textures: never lock one the GPU may be reading
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(t->LockRect(0, &lr, NULL, 0))) {
        for (int y = 0; y < 240; y++) memcpy((uint8_t *)lr.pBits + y * lr.Pitch, img + y * 320, 320 * 4);
        t->UnlockRect(0);
    }
    // the picture's size in screen pixels (the screen's pixels may be non-square at 480 lines)
    float aspect = (float)screen_w / screen_h;               // as the TV shows it: 16:9 or 4:3
    float tv_aspect = screen_w == 640 ? 4.0f / 3.0f : 16.0f / 9.0f;
    float pix_w = aspect / tv_aspect;                        // width of a screen pixel, in heights
    float ph = (float)screen_h, pw = ph * 4.0f / 3.0f / pix_w;
    int view = raw ? VIEW_43 : opt.view;
    if (view == VIEW_ZOOM && pw < screen_w) { float k = screen_w / pw; pw *= k; ph *= k; }
    if (pw > screen_w && view != VIEW_ZOOM) { float k = screen_w / pw; pw *= k; ph *= k; }
    if (view == VIEW_WIDE) { pw = (float)screen_w; ph = (float)screen_h; }

    const float size[4] = { 320, 240, 1.0f / 320, 1.0f / 240 };
    dev->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1.0f, 0);
    dev->SetVertexDeclaration(decl);
    dev->SetVertexShader(vs);
    dev->SetTexture(0, t);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetPixelShaderConstantF(0, size, 1);
    if (view == VIEW_43_SIDES && pw < screen_w) {
        dev->SetPixelShader(ps_bg);
        draw_quad(1.0f, 1.0f);
    }
    float f1[4] = { (float)(raw ? FILTER_SHARP : opt.filter), opt.scanlines / 10.0f, ph / 240.0f,
                    raw ? 0.0f : opt.glow / 10.0f };
    float f2[4] = { raw ? 1.0f : opt.bright / 100.0f, raw ? 1.0f : opt.sat / 100.0f,
                    raw ? 1.0f : 100.0f / (float)opt.gamma, pw / 320.0f };
    dev->SetPixelShaderConstantF(1, f1, 1);
    dev->SetPixelShaderConstantF(2, f2, 1);
    dev->SetPixelShader(ps);
    draw_quad(pw / screen_w, ph / screen_h);
    dev->SetTexture(0, NULL);
    dev->Present(NULL, NULL, NULL, NULL);
}

static void present(const uint32_t *img) { present_ex(img, true); }

/* ---------------------------------------------------------------- */
/* Audio                                                            */
/* ---------------------------------------------------------------- */

// The emulation thread never calls XAudio2 (its calls wait on XAudio2's own
// thread and cost milliseconds per frame there): it writes samples into a
// ring, the main thread hands them to the voice, and a voice callback counts
// what has been played.
#define NBUF 32
#define BUF_SAMPLES 2048
#define RING (1 << 16)                  // samples, about 2 s
static IXAudio2 *xa;
static IXAudio2SourceVoice *voice;
static int16_t abuf[NBUF][BUF_SAMPLES];
static int abuf_next;
static int16_t ring[RING];
static volatile uint64_t ring_w;        // samples written (emulation thread)
static uint64_t ring_r;                 // samples submitted (main thread)
static volatile uint64_t played;        // samples finished playing (voice callback)
static double voice_rate;               // the rate the voice was created at
// The DCS's current output rate, published by the emulation thread: the main
// thread must never call sound_rate() itself, which runs pending DSP cycles
// (it would run the sound board on two threads at once).
static volatile double dcs_rate;

struct VoiceCallback : IXAudio2VoiceCallback {
    void __stdcall OnBufferEnd(void *ctx) { played += (uint64_t)(uintptr_t)ctx; }
    void __stdcall OnVoiceProcessingPassStart(UINT32) {}
    void __stdcall OnVoiceProcessingPassEnd() {}
    void __stdcall OnStreamEnd() {}
    void __stdcall OnBufferStart(void *) {}
    void __stdcall OnLoopEnd(void *) {}
    void __stdcall OnVoiceError(void *, HRESULT) {}
};
static VoiceCallback voice_cb;

static void audio_init(void)
{
    if (FAILED(XAudio2Create(&xa, 0, XboxThread4))) { xa = NULL; return; }
    IXAudio2MasteringVoice *mv;
    if (FAILED(xa->CreateMasteringVoice(&mv, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, 0, NULL))) return;
    voice_rate = sound_rate();
    WAVEFORMATEX wf;
    memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 1;
    wf.nSamplesPerSec = (DWORD)voice_rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2;
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * 2;
    if (FAILED(xa->CreateSourceVoice(&voice, &wf, 0, 4.0f, &voice_cb, NULL, NULL))) { voice = NULL; return; }
    voice->Start(0, 0);
}

// Frames of sound kept queued ahead of the speaker (the emulation thread runs
// the next frame when fewer are): about 70 ms, enough to ride over a slow frame.
#define AUDIO_AHEAD 4

// Sound written but not yet played, in game frames (-1 without sound).
static double audio_queued_frames(void)
{
    if (!voice) return -1;
    return (double)(ring_w - played) * FRAME_HZ / voice_rate;
}

// Emulation thread: this frame's DCS output into the ring.
static void audio_frame(void)
{
    static int16_t in[BUF_SAMPLES];
    int n = sound_fetch_async(in, BUF_SAMPLES);   // what the DSP has made so far: never wait for it here
    dcs_rate = sound_rate_async();
    if (!voice || n <= 0) return;
    uint64_t w = ring_w;
    if (w + (uint64_t)n - ring_r > RING) return;            // the main thread is stuck: drop it
    for (int i = 0; i < n; i++) ring[(w + (uint64_t)i) & (RING - 1)] = in[i];
    __lwsync();                                 // the samples are in memory before the count says so
    ring_w = w + (uint64_t)n;
}

// Main thread: hand what the emulation wrote to the voice (16-bit native
// PCM, as XAudio2 wants it here). Below the target queue (a scene heavier
// than real time) the sound is played slower, up to 15%, smoothed so the
// pitch glides: it slows with the game instead of breaking up into gaps.
static void audio_pump(void)
{
    static float last_ratio;
    static double slow;                         // smoothed slowdown, 0 to 1
    if (!voice) return;
    uint64_t w = ring_w;
    __lwsync();
    while (ring_r < w) {
        uint64_t n = w - ring_r;
        if (n > BUF_SAMPLES) n = BUF_SAMPLES;
        if (ring_r + n > played + (uint64_t)(NBUF - 1) * BUF_SAMPLES) break;   // all buffers busy
        int16_t *b = abuf[abuf_next];
        const int gain = opt.sound;             // here, not in the ring: the emulation's sound stays as it is
        for (uint64_t i = 0; i < n; i++) {
            int v = (int)ring[(ring_r + i) & (RING - 1)] * gain / 100;
            b[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        XAUDIO2_BUFFER xb;
        memset(&xb, 0, sizeof xb);
        xb.AudioBytes = (UINT32)n * 2;
        xb.pAudioData = (const BYTE *)b;
        xb.pContext = (void *)(uintptr_t)n;     // OnBufferEnd adds it to `played`
        if (FAILED(voice->SubmitSourceBuffer(&xb, NULL))) break;
        abuf_next = (abuf_next + 1) % NBUF;
        ring_r += n;
    }
    double target = (AUDIO_AHEAD + 0.5) * voice_rate / FRAME_HZ;
    double err = ((double)(ring_w - played) - target) / target;
    if (err > 0) err = 0;                       // never faster: the pacing keeps it full
    if (err < -1) err = -1;
    slow += (-err - slow) * 0.05;               // the pump runs about every millisecond
    double rate = dcs_rate > 0 ? dcs_rate : voice_rate;
    float ratio = (float)(rate / voice_rate * (1 - 0.15 * slow));
    if (fabsf(ratio - last_ratio) > 0.0005f) { voice->SetFrequencyRatio(ratio); last_ratio = ratio; }
}

/* ---------------------------------------------------------------- */
/* Input                                                            */
/* ---------------------------------------------------------------- */

// Diagnostic builds (build360.ps1 -Diag): checkpoints against the PC
// reference, the disk log on screen, the overlay on from the start, and the
// disk's own settings sectors.
#ifdef UC_DIAG
static const bool diag = true;
#else
static const bool diag = false;
#endif
static volatile bool quit_requested;     // set by either thread
extern double machine_sound_ms;          // machine.c, UC_PROFILE

// XInputGetState for a pad, asking an empty port again only every 2 s (a
// call for a missing pad can be slow). `skip` is the caller's own counter
// for that port: the two threads each keep theirs.
static bool get_pad(DWORD i, XINPUT_STATE *s, int *skip)
{
    if (*skip > 0) { --*skip; return false; }
    if (XInputGetState(i, s) == ERROR_SUCCESS) return true;
    *skip = 120;
    return false;
}

static uint32_t pad_bits(DWORD i, bool *test, bool *quit, bool *speed)
{
    static int skip[2];
    XINPUT_STATE s;
    if (!get_pad(i, &s, &skip[i])) return 0;
    return pad_translate((int)i, s.Gamepad, test, quit, speed);
}

static volatile bool menu_open;          // the main thread opens it; the emulation thread then waits

static void read_input(void)
{
    static int quit_frames;
    static bool speed_was;
    bool test = false, quit = false, speed = false;
    uint32_t p1 = pad_bits(0, &test, &quit, &speed), p2 = pad_bits(1, &test, &quit, &speed);
    if (speed && !speed_was) opt.show_speed = !opt.show_speed;
    speed_was = speed;
    if (menu_open) p1 = p2 = 0, test = false;     // the menu has the pads
    hw.p1 = ~p1;
    hw.p2 = ~p2;
    if (test) hw.p1 &= ~0x1000u;
    hw.volume = 0xffffffffu;
    quit_frames = quit ? quit_frames + 1 : 0;
    if (quit_frames > 2 * 60) quit_requested = true;
}

static char error_text[2048];

/* ---------------------------------------------------------------- */
/* Settings menu                                                    */
/* ---------------------------------------------------------------- */

static const char INI_PATH[] = "game:\\saves\\kix360.ini";

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// False when there is no settings file, or it doesn't name a language yet
// (the language screen is shown before the game then).
static bool settings_load(void)
{
    FILE *f = fopen(INI_PATH, "r");
    if (!f) return false;
    bool have_lang = false;
    int pad_lines = 0;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char key[32];
        int v;
        if (sscanf(line, "%31[^=]=%d", key, &v) != 2) continue;
        if (!strcmp(key, "view")) opt.view = clampi(v, 0, VIEW_N - 1);
        else if (!strcmp(key, "filter")) opt.filter = clampi(v, 0, FILTER_N - 1);
        else if (!strcmp(key, "scanlines")) opt.scanlines = clampi(v, 0, 10);
        else if (!strcmp(key, "glow")) opt.glow = clampi(v, 0, 10);
        else if (!strcmp(key, "brightness")) opt.bright = clampi(v, 50, 150);
        else if (!strcmp(key, "saturation")) opt.sat = clampi(v, 0, 200);
        else if (!strcmp(key, "gamma")) opt.gamma = clampi(v, 70, 180);
        else if (!strcmp(key, "show_speed")) opt.show_speed = v != 0;
        else if (!strcmp(key, "sound_volume")) opt.sound = clampi(v, 50, 300);
        else if (!strcmp(key, "language")) { ui_set_lang(v); have_lang = true; }
        else if (padmap_load_key(key, v)) pad_lines++;
    }
    fclose(f);
    if (padmap_check()) fprintf(stderr, "buttons: the file repeats a button, defaults used\n");
    padmap_chosen();
    char text[96];
    padmap_text(text, sizeof text);
    fprintf(stderr, "buttons from the file (%d lines): %s\n", pad_lines, text);
    return have_lang;
}

static void settings_save(void)
{
    _mkdir("game:\\saves");                     // the language screen comes before the boot makes it
    FILE *f = fopen(INI_PATH, "w");
    if (!f) { fprintf(stderr, "settings: can't open %s to write\n", INI_PATH); return; }
    fprintf(f, "; KI-X360 settings (the menu writes this file: right stick click in game)\n");
    fprintf(f, "; view: 0 = 4:3, 1 = 4:3 with blurred sides, 2 = 16:9 full (stretched, nothing cut), 3 = 16:9 zoom (crops top and bottom)\n");
    fprintf(f, "view=%d\n", opt.view);
    fprintf(f, "; filter: 0 = sharp pixels, 1 = smooth, 2 = CRT\n");
    fprintf(f, "filter=%d\n", opt.filter);
    fprintf(f, "; scanlines and glow: 0-10; brightness 50-150, saturation 0-200 (percent)\n");
    fprintf(f, "scanlines=%d\nglow=%d\nbrightness=%d\nsaturation=%d\n", opt.scanlines, opt.glow, opt.bright, opt.sat);
    fprintf(f, "; gamma in hundredths, 70-180: above 100 lifts the mid tones (TVs show the picture darker)\n");
    fprintf(f, "gamma=%d\n", opt.gamma);
    fprintf(f, "show_speed=%d\n", opt.show_speed ? 1 : 0);
    fprintf(f, "; sound volume in percent, 50-300: 100 is the arcade board's output as it is\n");
    fprintf(f, "sound_volume=%d\n", opt.sound);
    fprintf(f, "; language: 0 = English, 1 = Portugues (Brasil)\n");
    fprintf(f, "language=%d\n", ui_lang());
    padmap_save(f);
    bool ok = !ferror(f);
    ok &= fclose(f) == 0;
    char text[96];
    padmap_text(text, sizeof text);
    fprintf(stderr, "settings %s, buttons %s\n", ok ? "saved" : "NOT SAVED (write failed)", text);
}

// One pad's buttons this frame, and those just pressed (both pads).
static WORD pads_down, pads_pressed;

static void poll_menu_pads(void)
{
    static int skip[2];
    WORD now = 0;
    for (DWORD i = 0; i < 2; i++) {
        XINPUT_STATE s;
        if (!get_pad(i, &s, &skip[i])) continue;
        WORD b = pad_held(s.Gamepad);           // the button screen needs the triggers too
        const SHORT dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        if (s.Gamepad.sThumbLY > dz) b |= XINPUT_GAMEPAD_DPAD_UP;
        if (s.Gamepad.sThumbLY < -dz) b |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (s.Gamepad.sThumbLX < -dz) b |= XINPUT_GAMEPAD_DPAD_LEFT;
        if (s.Gamepad.sThumbLX > dz) b |= XINPUT_GAMEPAD_DPAD_RIGHT;
        now |= b;
    }
    // held directions repeat, so values can be swept quickly
    static int held;
    const WORD dirs = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT;
    pads_pressed = now & ~pads_down;
    if (now & dirs & pads_down) { if (++held > 20 && held % 5 == 0) pads_pressed |= now & dirs; }
    else held = 0;
    pads_down = now;
}

enum { M_VIEW, M_FILTER, M_SCAN, M_GLOW, M_BRIGHT, M_GAMMA, M_SAT, M_SOUND, M_SPEED, M_LANG, M_BUTTONS, M_BACK, M_QUIT, M_N };
static int menu_sel;
// The button screen, a page of the menu: which player, and the attack waiting for a button (-1: none).
enum { C_PLAYER, C_ATTACK, C_DEFAULTS = C_ATTACK + PAD_ATTACKS, C_BACK, C_N };
static bool buttons_open;
static int buttons_sel, buttons_player, buttons_wait = -1;

// Change the selected item by d (-1 or +1).
static void menu_change(int d)
{
    switch (menu_sel) {
    case M_VIEW: opt.view = (opt.view + d + VIEW_N) % VIEW_N; break;
    case M_FILTER: opt.filter = (opt.filter + d + FILTER_N) % FILTER_N; break;
    case M_SCAN: opt.scanlines = clampi(opt.scanlines + d, 0, 10); break;
    case M_GLOW: opt.glow = clampi(opt.glow + d, 0, 10); break;
    case M_BRIGHT: opt.bright = clampi(opt.bright + 5 * d, 50, 150); break;
    case M_SAT: opt.sat = clampi(opt.sat + 10 * d, 0, 200); break;
    case M_GAMMA: opt.gamma = clampi(opt.gamma + 5 * d, 70, 180); break;
    case M_SOUND: opt.sound = clampi(opt.sound + 25 * d, 50, 300); break;
    case M_SPEED: opt.show_speed = !opt.show_speed; break;
    case M_LANG: ui_set_lang((ui_lang() + d + LANG_N) % LANG_N); break;
    }
}

static void menu_close(void)
{
    buttons_open = false;
    buttons_wait = -1;
    menu_open = false;
    settings_save();
    if (voice) voice->Start(0, 0);
}

// One frame of the button screen. Any pad may answer, so one person can set
// up both players. Only the attacks' buttons can be taken: Start and Back
// cancel, and the menu's own way in (right stick click) stays free.
static void buttons_frame(const uint32_t *game_img)
{
    if (buttons_wait >= 0) {
        if (pads_pressed & (XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK)) buttons_wait = -1;
        else for (int i = 0; i < PAD_INPUTS; i++)
            if (pads_pressed & pad_inputs[i].mask) {
                padmap_assign(buttons_player, buttons_wait, i);
                padmap_chosen();
                settings_save();                // kept from this moment, whatever happens next
                buttons_wait = -1;
                break;
            }
    } else {
        if (pads_pressed & (XINPUT_GAMEPAD_RIGHT_THUMB | XINPUT_GAMEPAD_START)) { menu_close(); return; }
        if (pads_pressed & XINPUT_GAMEPAD_B) { buttons_open = false; return; }
        if (pads_pressed & XINPUT_GAMEPAD_DPAD_UP) buttons_sel = (buttons_sel + C_N - 1) % C_N;
        if (pads_pressed & XINPUT_GAMEPAD_DPAD_DOWN) buttons_sel = (buttons_sel + 1) % C_N;
        if (buttons_sel == C_PLAYER && (pads_pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)))
            buttons_player ^= 1;
        if (pads_pressed & XINPUT_GAMEPAD_A) {
            if (buttons_sel == C_PLAYER) buttons_player ^= 1;
            else if (buttons_sel == C_DEFAULTS) { padmap_reset(buttons_player); padmap_chosen(); settings_save(); }
            else if (buttons_sel == C_BACK) { buttons_open = false; return; }
            else buttons_wait = buttons_sel - C_ATTACK;
        }
    }

    const char *const attacks[PAD_ATTACKS] = {
        tr("QUICK PUNCH", "SOCO FRACO"), tr("MEDIUM PUNCH", "SOCO MEDIO"), tr("FIERCE PUNCH", "SOCO FORTE"),
        tr("QUICK KICK", "CHUTE FRACO"), tr("MEDIUM KICK", "CHUTE MEDIO"), tr("FIERCE KICK", "CHUTE FORTE"),
    };
    char it[C_N][64];
    snprintf(it[C_PLAYER], 64, "%s\t%d", tr("PLAYER", "JOGADOR"), buttons_player + 1);
    for (int a = 0; a < PAD_ATTACKS; a++)
        snprintf(it[C_ATTACK + a], 64, "%s\t%s", attacks[a], a == buttons_wait ? "..." : pad_inputs[pad_map[buttons_player][a]].name);
    snprintf(it[C_DEFAULTS], 64, "%s", tr("RESTORE DEFAULTS", "VOLTAR AO PADRAO"));
    snprintf(it[C_BACK], 64, "%s", tr("BACK", "VOLTAR"));
    const char *items[C_N];
    for (int i = 0; i < C_N; i++) items[i] = it[i];

    static uint32_t img[320 * 240];
    memcpy(img, game_img, sizeof img);
    const char *footer = buttons_wait >= 0 ? tr("PRESS A BUTTON  START: CANCEL", "APERTE UM BOTAO  START: CANCELA")
                       : buttons_sel == C_PLAYER ? tr("LEFT/RIGHT: CHANGE PLAYER", "ESQ/DIR: TROCA O JOGADOR")
                       : tr("A: CHANGE  B: BACK", "A: MUDAR  B: VOLTAR");
    osd_menu(img, tr("CONTROLLER BUTTONS", "BOTOES DO CONTROLE"), items, C_N, buttons_sel, footer);
    present_ex(img, false);
}

// One frame of the open menu: input, then the panel over the paused game.
static void menu_frame(const uint32_t *game_img)
{
    poll_menu_pads();
    if (buttons_open) {
        buttons_frame(game_img);
        if (buttons_open || !menu_open) return;
        pads_pressed = 0;                       // back on the main page: that press is spent
    }
    if (pads_pressed & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_RIGHT_THUMB | XINPUT_GAMEPAD_START)) { menu_close(); return; }
    if (pads_pressed & XINPUT_GAMEPAD_DPAD_UP) menu_sel = (menu_sel + M_N - 1) % M_N;
    if (pads_pressed & XINPUT_GAMEPAD_DPAD_DOWN) menu_sel = (menu_sel + 1) % M_N;
    if (pads_pressed & XINPUT_GAMEPAD_DPAD_LEFT) menu_change(-1);
    if (pads_pressed & XINPUT_GAMEPAD_DPAD_RIGHT) menu_change(+1);
    if (pads_pressed & XINPUT_GAMEPAD_A) {
        if (menu_sel == M_BACK) { menu_close(); return; }
        if (menu_sel == M_QUIT) { menu_close(); quit_requested = true; return; }
        if (menu_sel == M_BUTTONS) { buttons_open = true; buttons_sel = 0; buttons_wait = -1; pads_pressed = 0; buttons_frame(game_img); return; }
        menu_change(+1);
    }

    const char *views[VIEW_N] = { tr("4:3 ORIGINAL", "4:3 ORIGINAL"), tr("4:3 + SIDE FILL", "4:3 + LATERAIS"),
                                  tr("16:9 FULL", "16:9 TELA CHEIA"), tr("16:9 ZOOM", "ZOOM 16:9") };
    const char *filters[FILTER_N] = { tr("SHARP", "NITIDO"), tr("SMOOTH", "SUAVE"), "CRT" };
    char it[M_N][64];
    snprintf(it[M_VIEW], 64, "%s\t%s", tr("SCREEN", "TELA"), views[opt.view]);
    snprintf(it[M_FILTER], 64, "%s\t%s", tr("FILTER", "FILTRO"), filters[opt.filter]);
    if (opt.filter == FILTER_CRT) snprintf(it[M_SCAN], 64, "SCANLINES\t%d", opt.scanlines);
    else snprintf(it[M_SCAN], 64, "SCANLINES\t%s", tr("(CRT ONLY)", "(SO NO CRT)"));
    if (opt.glow) snprintf(it[M_GLOW], 64, "%s\t%d", tr("GLOW", "BRILHO DE LUZ (GLOW)"), opt.glow);
    else snprintf(it[M_GLOW], 64, "%s\t%s", tr("GLOW", "BRILHO DE LUZ (GLOW)"), tr("OFF", "DESLIGADO"));
    snprintf(it[M_BRIGHT], 64, "%s\t%d%%", tr("BRIGHTNESS", "LUMINOSIDADE"), opt.bright);
    snprintf(it[M_SAT], 64, "%s\t%d%%", tr("COLOUR SATURATION", "SATURACAO DAS CORES"), opt.sat);
    snprintf(it[M_GAMMA], 64, "%s\t%d.%02d", tr("GAMMA (MID TONES)", "GAMMA (TONS MEDIOS)"), opt.gamma / 100, opt.gamma % 100);
    snprintf(it[M_SOUND], 64, "%s\t%d%%", tr("SOUND VOLUME", "VOLUME DO SOM"), opt.sound);
    snprintf(it[M_SPEED], 64, "%s\t%s", tr("SHOW SPEED", "MOSTRAR VELOCIDADE"),
             opt.show_speed ? tr("YES", "SIM") : tr("NO", "NAO"));
    snprintf(it[M_LANG], 64, "%s\t%s", tr("LANGUAGE", "IDIOMA"), tr("ENGLISH", "PORTUGUES (BR)"));
    snprintf(it[M_BUTTONS], 64, "%s", tr("CONTROLLER BUTTONS", "BOTOES DO CONTROLE"));
    snprintf(it[M_BACK], 64, "%s", tr("BACK TO THE GAME", "VOLTAR AO JOGO"));
    snprintf(it[M_QUIT], 64, "%s", tr("QUIT KI-X360", "SAIR DO KI-X360"));
    const char *items[M_N];
    for (int i = 0; i < M_N; i++) items[i] = it[i];

    // the paused picture with the chosen effects behind the panel, so changes show at once
    static uint32_t img[320 * 240];
    memcpy(img, game_img, sizeof img);
    const char *footer = menu_sel == M_VIEW && opt.view == VIEW_ZOOM
        ? tr("ZOOM HIDES THE LIFE BARS AND FEET", "ZOOM ESCONDE AS BARRAS DE VIDA E OS PES")
        : menu_sel == M_VIEW && opt.view == VIEW_WIDE
        ? tr("STRETCHED TO FILL, NOTHING CUT", "ESTICADO PARA ENCHER, SEM CORTAR")
        : menu_sel == M_SOUND
        ? tr("HEARD WHEN BACK IN THE GAME", "OUVE AO VOLTAR PARA O JOGO")
        : tr("A/B: OK/BACK  LEFT/RIGHT: CHANGE", "A/B: OK/VOLTAR  ESQ/DIR: MUDAR");
    osd_menu(img, tr("KI-X360  SETTINGS", "KI-X360  CONFIGURACOES"), items, M_N, menu_sel, footer);
    present_ex(img, false);
}

/* ---------------------------------------------------------------- */
/* Main loop                                                        */
/* ---------------------------------------------------------------- */

// A text screen: the title, then `text` line by line (it is not changed),
// and a footer.
static void show_text(const char *text, const char *footer)
{
    static uint32_t img[320 * 240];
    memset(img, 0, sizeof img);
    osd_text(img, 8, 8, "KI-X360", 0xffcc00);
    int y = 28;
    for (const char *line = text; *line && y < 210; y += 10) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        char buf[64];
        snprintf(buf, sizeof buf, "%.*s", (int)len, line);
        osd_text(img, 8, y, buf, 0xffffff);
        line += len + (nl ? 1 : 0);
    }
    if (footer) osd_text(img, 8, 220, footer, 0xa0a0a0);
    present(img);
}

// Emulation speed: the share of real time one game frame takes, measured
// around run_frame only (presenting waits for the vertical blank).
static double speed_pct, frame_ms, interp_pct, sound_ms;
/* ---------------------------------------------------------------- */
/* Log                                                              */
/* ---------------------------------------------------------------- */

// Everything the emulator writes to stderr arrives here (x360_compat.h): the
// last lines are kept for the screen, and the rest waits in memory until
// log_commit() appends it to the file. The console's file system records a
// file's length only when it is closed, so each commit opens and closes it.
static bool log_ok = true;
static const char LOG_PATH[] = "game:\\kix360.log";
static char log_pending[64 * 1024];
static size_t log_pending_len;
enum { LOG_LINES = 20, LOG_COLS = 53 };          // 53 characters fill the 320-pixel screen
static char log_lines[LOG_LINES][LOG_COLS + 1];
static int log_next, log_col;
// Both threads write to the log; only the main thread writes the file, so the
// emulation thread never waits for the drive.
static CRITICAL_SECTION log_cs;

static void log_init(void) { InitializeCriticalSection(&log_cs); }

extern "C" void x360_log_write(const char *s)
{
    EnterCriticalSection(&log_cs);
    size_t n = strlen(s);
    if (n > sizeof log_pending - log_pending_len) n = sizeof log_pending - log_pending_len;
    memcpy(log_pending + log_pending_len, s, n);
    log_pending_len += n;
    for (; *s; s++) {
        if (*s == '\n') { log_next = (log_next + 1) % LOG_LINES; log_col = 0; log_lines[log_next][0] = 0; continue; }
        if (log_col < LOG_COLS) { log_lines[log_next][log_col++] = *s; log_lines[log_next][log_col] = 0; }
    }
    LeaveCriticalSection(&log_cs);
}

static void log_commit(void)
{
    static char out[sizeof log_pending];
    EnterCriticalSection(&log_cs);
    size_t n = log_pending_len;
    memcpy(out, log_pending, n);
    log_pending_len = 0;
    LeaveCriticalSection(&log_cs);
    if (!n) return;
    FILE *f = fopen(LOG_PATH, "ab");
    if (f) {
        log_ok = fwrite(out, 1, n, f) == n;
        log_ok &= fclose(f) == 0;
    } else {
        log_ok = false;
    }
}

static void draw_log(uint32_t *img, int y)
{
    int rows = (240 - y) / 10;
    if (rows > LOG_LINES - 1) rows = LOG_LINES - 1;
    osd_box(img, 0, y - 1, 320, rows * 10 + 2, 0xc0000000u);
    for (int i = 0; i < rows; i++) {
        const char *l = log_lines[(log_next - rows + i + LOG_LINES) % LOG_LINES];   // complete lines only
        osd_text(img, 1, y + i * 10, l, 0xc0c0c0);
    }
}

// Comparison with the PC reference: the first checkpoint that differs, and
// which parts (RAM0, RAM2, CPU, DSP) differ there.
static uint64_t ck_last_ok, ck_bad_frame;
static int ck_bad_mask;

static void checkpoint(uint64_t frames)
{
    if (!checkpoint_frame(frames)) return;
    unsigned long long h[CK_PARTS];
    checkpoint_hashes(h);
    int diff = checkpoint_check(frames, h);
    fprintf(stderr, "checkpoint %llu: %016llx %016llx %016llx %016llx pc=%08x %s\n", (unsigned long long)frames,
            h[0], h[1], h[2], h[3], cpu.pc, diff < 0 ? "" : diff ? "DIFFERS" : "ok");
    if (diff == 0 && !ck_bad_frame) ck_last_ok = frames;
    if (diff > 0 && !ck_bad_frame) { ck_bad_frame = frames; ck_bad_mask = diff; }
}

static void measure(uint64_t t0, uint64_t t1, uint64_t frames)
{
    static LARGE_INTEGER freq;
    static double acc;
    static int n;
    static uint64_t last_cyc, last_int;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    acc += (double)(t1 - t0) * 1000.0 / (double)freq.QuadPart;
    if (diag) checkpoint(frames);
    if (++n < 30) return;
    frame_ms = acc / n;
    speed_pct = 100.0 * (1000.0 / FRAME_HZ) / frame_ms;
    sound_ms = machine_sound_ms / n;
    machine_sound_ms = 0;
    interp_pct = 100.0 * (double)(cpu_interp_count - last_int) / (double)(cpu.cycles - last_cyc + 1);
    last_cyc = cpu.cycles; last_int = cpu_interp_count;
    acc = 0; n = 0;
    if (frames <= 600 || frames % 300 < 30)
        fprintf(stderr, "frame %llu: %.2f ms/frame (sound %.2f), %.0f%% speed, %.0f%% interpreted, pc=%08x\n",
                (unsigned long long)frames, frame_ms, sound_ms, speed_pct, interp_pct, cpu.pc);
}

/* ---------------------------------------------------------------- */
/* Emulation thread                                                 */
/* ---------------------------------------------------------------- */

// The machine runs on its own core, paced by the clock at the arcade's frame
// rate, and hands each finished frame (the guest's 16-bit pixels) to the main
// thread, which converts, draws and presents it. Presenting waits for the
// vertical blank, which must never hold the emulation (or its sound) back.
static CRITICAL_SECTION frame_cs;
// Three picture buffers: the machine writes one nobody reads, then (under the
// lock, briefly) marks it the newest; the main thread shows the newest in
// place, without copying it.
static uint16_t frame_bufs[3][320 * 240];
static int fb_write = 0, fb_ready = 0, fb_reading = -1;
static volatile uint32_t frame_seq;
static volatile uint64_t emu_frames;
// Where the emulation thread is, shown by the main thread (which keeps running
// if the other one stops): 1 waiting for sound to play, 2 reading the pads,
// 3 running the frame, 4 passing its sound on, 5 saving settings, 6 handing
// the picture over.
static volatile int emu_stage;

static DWORD WINAPI emu_thread(LPVOID)
{
    LARGE_INTEGER freq, now, t0, t1, t2;
    QueryPerformanceFrequency(&freq);
    const double tick_ms = 1000.0 / (double)freq.QuadPart;
    const double period = (double)freq.QuadPart / FRAME_HZ;
    QueryPerformanceCounter(&now);
    double next = (double)now.QuadPart;
    uint64_t frames = 0;
    double wait_ms = 0, other_ms = 0;           // per-frame costs outside run_frame, for the log
    double sound_ms_acc = 0, copy_ms_acc = 0;   // ...split: passing the sound on, handing the picture over
    while (!quit_requested) {
        if (menu_open) {                        // paused: start the clock afresh afterwards
            Sleep(10);
            QueryPerformanceCounter(&now);
            next = (double)now.QuadPart;
            continue;
        }
        // The sound card's clock paces the game: the next frame runs as soon
        // as less than AUDIO_AHEAD frames of sound are queued. When the CPU
        // keeps up, the game runs at exactly its speed and the sound never
        // runs dry. Waiting spins: Sleep on the console can oversleep by
        // several milliseconds, a large part of a frame. Without sound, the
        // clock paces it instead.
        emu_stage = 1;
        QueryPerformanceCounter(&t0);
        double q = audio_queued_frames();
        if (q >= 0) {
            while (q > AUDIO_AHEAD && !menu_open && !quit_requested) {
                // spin until about the time enough has played
                double until = (double)t0.QuadPart + (q - AUDIO_AHEAD) * period;
                do QueryPerformanceCounter(&now); while ((double)now.QuadPart < until);
                q = audio_queued_frames();
                t0 = now;
            }
        } else {
            QueryPerformanceCounter(&now);
            if (next - (double)now.QuadPart > 0) do QueryPerformanceCounter(&now); while ((double)now.QuadPart < next);
            else if ((double)now.QuadPart - next > 4 * period) next = (double)now.QuadPart;
            next += period;
        }
        QueryPerformanceCounter(&t1);
        wait_ms += (double)(t1.QuadPart - t0.QuadPart) * tick_ms;

        emu_stage = 2;
        read_input();
        emu_stage = 3;
        QueryPerformanceCounter(&t0);
        run_frame();
        QueryPerformanceCounter(&t1);
        measure((uint64_t)t0.QuadPart, (uint64_t)t1.QuadPart, ++frames);
        emu_frames = frames;
        emu_stage = 4;
        LARGE_INTEGER ta, tb;
        QueryPerformanceCounter(&ta);
        audio_frame();                          // includes waiting for the sound DSP to finish the frame
        QueryPerformanceCounter(&tb);
        sound_ms_acc += (double)(tb.QuadPart - ta.QuadPart) * tick_ms;
        emu_stage = 5;
        if (frames % (60 * 60) == 0) ide_flush();       // settings and high scores, once a minute
        emu_stage = 6;
        memcpy(frame_bufs[fb_write], video_base(), sizeof frame_bufs[0]);   // a buffer nobody reads
        EnterCriticalSection(&frame_cs);
        fb_ready = fb_write;
        for (int i = 0; i < 3; i++)                     // next: neither the newest nor the one being shown
            if (i != fb_ready && i != fb_reading) { fb_write = i; break; }
        frame_seq++;
        LeaveCriticalSection(&frame_cs);
        QueryPerformanceCounter(&t2);
        copy_ms_acc += (double)(t2.QuadPart - tb.QuadPart) * tick_ms;
        other_ms += (double)(t2.QuadPart - t1.QuadPart) * tick_ms;
        if (frames % 300 == 0) {
            fprintf(stderr, "frame %llu: waited %.2f ms/frame, after the frame %.2f ms/frame (sound %.2f, picture %.2f), "
                    "sound queued %.1f frames\n", (unsigned long long)frames, wait_ms / 300, other_ms / 300,
                    sound_ms_acc / 300, copy_ms_acc / 300, audio_queued_frames());
            wait_ms = other_ms = sound_ms_acc = copy_ms_acc = 0;
        }
    }
    return 0;
}

// Text twice the OSD font's size, styled like the game's own lettering: each
// dot a 2x2 block, a dark outline, and a gold-to-orange gradient from top to
// bottom (green-gold when `good`: full speed).
static void draw_big_text(uint32_t *img, int x0, int y0, const char *s, bool good)
{
    const uint32_t top = good ? 0xfff8a0 : 0xfff070, bottom = good ? 0x60d020 : 0xe06010;
    for (int pass = 0; pass < 2; pass++)            // outline first, then the letters over it
        for (int i = 0; s[i]; i++) {
            const uint8_t *g = osd_glyph(s[i]);
            for (int col = 0; col < 5; col++)
                for (int row = 0; row < 7; row++) {
                    if (!(g[col] >> row & 1)) continue;
                    int x = x0 + i * 12 + col * 2, y = y0 + row * 2;
                    if (pass == 0) {
                        for (int dy = -1; dy <= 2; dy++)
                            for (int dx = -1; dx <= 2; dx++)
                                if (x + dx >= 0 && x + dx < 320 && y + dy >= 0 && y + dy < 240)
                                    img[(y + dy) * 320 + x + dx] = 0x101010;
                        continue;
                    }
                    // the gradient by row, mixed per channel
                    uint32_t c = 0;
                    for (int sh = 0; sh < 24; sh += 8) {
                        int a = (int)(top >> sh & 255), b = (int)(bottom >> sh & 255);
                        c |= (uint32_t)(a + (b - a) * row / 6) << sh;
                    }
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++)
                            if (x + dx < 320 && y + dy < 240) img[(y + dy) * 320 + x + dx] = c;
                }
        }
}

// The guest's BGR555 pixels (little-endian words) as 0x00RRGGBB.
static void raw_to_rgb(const uint16_t *src, uint32_t *dst)
{
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t p = ld_le16(&src[i]);
        uint32_t r = (p & 31), g = (p >> 5) & 31, b = (p >> 10) & 31;
        dst[i] = (r << 19 | (r >> 2) << 16) | (g << 11 | (g >> 2) << 8) | (b << 3 | b >> 2);
    }
}

// Before the first start: English or Brazilian Portuguese. Shown when the
// settings file doesn't name a language yet; the menu changes it later.
static void choose_language(void)
{
    int sel = ui_lang();
    for (;;) {
        poll_menu_pads();
        if (pads_pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN)) sel ^= 1;
        if (pads_pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) break;
        static const char *const items[LANG_N] = { "ENGLISH", "PORTUGUES (BRASIL)" };
        static uint32_t img[320 * 240];
        memset(img, 0, sizeof img);
        osd_menu(img, "LANGUAGE / IDIOMA", items, LANG_N, sel, "A: OK");
        present(img);
    }
    ui_set_lang(sel);
    settings_save();
}

void __cdecl main()
{
    log_init();
    InitializeCriticalSection(&frame_cs);
    remove(LOG_PATH);                           // a fresh log each run
    if (!video_init()) return;
    if (!settings_load()) choose_language();
    if (diag) opt.show_speed = true;
    show_text(tr("LOADING...", "CARREGANDO..."), NULL);   // the disk image is read into memory first
    // Diagnostic builds start from the disk's own settings sectors, as the PC
    // reference does, so the checkpoints compare like with like.
    boot_set_pristine(diag);
    bool ok = boot_machine(error_text, sizeof error_text);
    if (!ok) fprintf(stderr, "%s\n", error_text);
    log_commit();
    HANDLE emu = NULL;
    if (ok) {
        audio_init();
        emu = CreateThread(NULL, 1 << 20, emu_thread, NULL, CREATE_SUSPENDED, NULL);
        XSetThreadProcessor(emu, 2);            // core 1; the main thread keeps core 0
        ResumeThread(emu);
    }
    static uint32_t game_px[320 * 240];         // the last game frame, without the overlay (behind the menu)
    uint32_t shown = 0;
    DWORD last_commit = GetTickCount(), rate_t0 = GetTickCount();
    uint64_t rate_f0 = 0;
    double game_pct = 0;                        // the game's real speed: frames run per second against the arcade's
    bool menu_was_open = false;
    while (!quit_requested) {
        if (!ok) { read_input(); show_text(error_text, tr("PRESS BACK + START TO QUIT", "BACK + START PARA SAIR")); continue; }
        if (menu_open) { menu_frame(game_px); menu_was_open = true; continue; }
        if (!padmap_guard()) fprintf(stderr, "buttons: changed outside the menu at frame %llu, chosen ones put back\n",
                                     (unsigned long long)emu_frames);
        DWORD t = GetTickCount();
        // the menu pauses the game: its time must not count against the speed shown
        if (menu_was_open) { menu_was_open = false; rate_t0 = t; rate_f0 = emu_frames; }
        if (t - rate_t0 >= 1000) {
            uint64_t f = emu_frames;
            game_pct = 100.0 * (double)(f - rate_f0) / ((t - rate_t0) / 1000.0) / FRAME_HZ;
            rate_f0 = f; rate_t0 = t;
        }
        if (t - last_commit >= 5000) {
            fprintf(stderr, "real speed %.0f%%  frame %llu stage %d  sound written %llu played %llu queued %.1f\n",
                    game_pct, (unsigned long long)emu_frames, emu_stage, (unsigned long long)ring_w,
                    (unsigned long long)played, audio_queued_frames());
            // exceptions taken so far, by MIPS code (0 interrupt, 4/5 address error, 8 syscall, 12 overflow...)
            char ex[256];
            int len = 0;
            for (int c = 0; c < 32; c++)
                if (cpu_exc_counts[c]) len += snprintf(ex + len, sizeof ex - (size_t)len, " %d:%u", c, cpu_exc_counts[c]);
            if (len) fprintf(stderr, "exceptions by code:%s  pc=%08x\n", ex, cpu.pc);
            log_commit();
            last_commit = t;
        }
        audio_pump();
        if (frame_seq == shown) { Sleep(1); continue; }
        EnterCriticalSection(&frame_cs);       // take the newest finished picture; it stays ours until the next
        fb_reading = fb_ready;
        shown = frame_seq;
        LeaveCriticalSection(&frame_cs);
        const uint16_t *raw = frame_bufs[fb_reading];
        // right stick click: the settings menu, over the paused game
        poll_menu_pads();
        if (pads_pressed & XINPUT_GAMEPAD_RIGHT_THUMB) {
            raw_to_rgb(raw, game_px);
            menu_open = true;
            menu_sel = 0;
            if (voice) voice->Stop(0, 0);
            continue;
        }
        raw_to_rgb(raw, px);
        if (opt.show_speed && !diag) {
            // big gold letters, like the game's own: frames per second and speed
            // bottom left, clear of the life bars (and above what 16:9 zoom crops)
            char big[32];
            snprintf(big, sizeof big, "%4.1f FPS %3.0f%%", game_pct * FRAME_HZ / 100.0, game_pct);
            int y = opt.view == VIEW_ZOOM ? 190 : 220;
            draw_big_text(px, 4, y, big, game_pct >= 97);
            char small[48];
            snprintf(small, sizeof small, "CPU %4.1f MS", frame_ms);
            osd_box(px, 4 + 12 * (int)strlen(big) + 4, y + 3, 6 * (int)strlen(small) + 4, 10, 0xa0000000u);
            osd_text(px, 4 + 12 * (int)strlen(big) + 6, y + 4, small, 0xe0e0e0);
        } else if (opt.show_speed) {
            // second line: what a photo of the screen needs to tell where the game is
            char line[64], info[64], ck[64];
            snprintf(line, sizeof line, "%s %3.0f%%  CPU %5.1f MS", tr("SPEED", "VELOCIDADE"), game_pct, frame_ms);
            if (diag)
                snprintf(info, sizeof info, "F%llu PC %08X ST%d Q%.1f W%lluK P%lluK", (unsigned long long)emu_frames,
                         cpu.pc, emu_stage, audio_queued_frames(), (unsigned long long)(ring_w / 1000),
                         (unsigned long long)(played / 1000));
            else
                snprintf(info, sizeof info, "F%llu PC %08X INT %.0f%% %s%s", (unsigned long long)emu_frames, cpu.pc,
                         interp_pct, rec_enabled ? "REC" : "NOREC", log_ok ? "" : " NOLOG");
            if (ck_bad_frame)
                snprintf(ck, sizeof ck, "DIFF F%llu:%s%s%s%s OK F%llu", (unsigned long long)ck_bad_frame,
                         ck_bad_mask & 1 << CK_RAM0 ? " RAM0" : "", ck_bad_mask & 1 << CK_RAM2 ? " RAM2" : "",
                         ck_bad_mask & 1 << CK_CPU ? " CPU" : "", ck_bad_mask & 1 << CK_DSP ? " DSP" : "",
                         (unsigned long long)ck_last_ok);
            else
                snprintf(ck, sizeof ck, "MATCH PC THRU F%llu", (unsigned long long)ck_last_ok);
            size_t w = strlen(line) > strlen(info) ? strlen(line) : strlen(info);
            if (diag && strlen(ck) > w) w = strlen(ck);
            osd_box(px, 0, 0, 6 * (int)w + 4, diag ? 31 : 21, 0xc0000000u);
            osd_text(px, 2, 2, line, game_pct >= 97 ? 0x40ff40 : 0xffff40);
            osd_text(px, 2, 12, info, 0xffffff);
            if (diag) {
                osd_text(px, 2, 22, ck, ck_bad_frame ? 0xff6060 : 0x40ff40);
                draw_log(px, 36);
            }
        }
        present_ex(px, false);
    }
    if (emu) WaitForSingleObject(emu, INFINITE);       // it stops at the end of its frame
    if (ok) ide_flush();
    log_commit();
    XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
}
