// PC test harness for the Xbox 360 port: the same emulator core and start-up
// (boot.cpp) built with VS2010 for x86, run headless. Used to check that the
// game boots, to time it, and to make the RAM snapshot the recompiler
// translates into the code linked into the .xex.
//
//   harness ROOT FRAMES [--ppm-every N] [--dump-at F]     (KI2_NOREC=1: interpreter only)
//   harness ROOT 0 --recomp RAM0 RAM2 OUTDIR
//
// ROOT holds the game files (kinst.zip, kinst\kinst.chd); images, dumps and
// the log go to the current folder.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../boot.h"
#include "machine.h"
#include "adsprec.h"
#include "cpu.h"
#include "mem.h"
#include "rec.h"
#include "recomp.h"
#include "sound.h"
#include "state.h"

static void save_ppm(const char *path)
{
    static uint32_t px[320 * 240];
    static uint8_t rgb[320 * 240 * 3];
    to_rgb(px);
    for (int i = 0; i < 320 * 240; i++) {
        rgb[3 * i] = (uint8_t)(px[i] >> 16); rgb[3 * i + 1] = (uint8_t)(px[i] >> 8); rgb[3 * i + 2] = (uint8_t)px[i];
    }
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 240\n255\n");
    fwrite(rgb, 1, sizeof rgb, f);
    fclose(f);
}

// --sample F1 F2: a sampling profiler for frames F1..F2. Another thread
// stops the emulation thread about once a millisecond and records where it
// was; at the end the addresses are named from the .pdb (dbghelp).
#include <dbghelp.h>
#include <mmsystem.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")
static HANDLE sample_target;
static volatile LONG sample_on, sample_quit;
static DWORD *sample_pc;
static volatile LONG sample_n;
enum { SAMPLE_MAX = 1 << 20 };

static DWORD WINAPI sample_thread(void *)
{
    timeBeginPeriod(1);
    while (!sample_quit) {
        Sleep(1);
        if (!sample_on || sample_n >= SAMPLE_MAX) continue;
        if (SuspendThread(sample_target) == (DWORD)-1) continue;
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(sample_target, &c)) sample_pc[sample_n++] = c.Eip;
        ResumeThread(sample_target);
    }
    return 0;
}

static int cmp_dword(const void *a, const void *b)
{
    DWORD x = *(const DWORD *)a, y = *(const DWORD *)b;
    return x < y ? -1 : x > y;
}

// samples per function, heaviest first
static void sample_report(void)
{
    int n = (int)sample_n;
    if (!n) return;
    qsort(sample_pc, (size_t)n, sizeof *sample_pc, cmp_dword);
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, NULL, TRUE);
    struct ent { char name[64]; int count; };
    static ent fn[4096];
    int nfn = 0;
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && sample_pc[j] == sample_pc[i]) j++;
        char buf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *si = (SYMBOL_INFO *)buf;
        memset(buf, 0, sizeof buf);
        si->SizeOfStruct = sizeof(SYMBOL_INFO);
        si->MaxNameLen = 255;
        DWORD64 disp = 0;
        char name[64];
        if (SymFromAddr(proc, sample_pc[i], &disp, si)) snprintf(name, sizeof name, "%s", si->Name);
        else snprintf(name, sizeof name, "?%08lx", (unsigned long)sample_pc[i]);
        int k = 0;
        while (k < nfn && strcmp(fn[k].name, name)) k++;
        if (k == nfn && nfn < 4096) { memcpy(fn[nfn].name, name, sizeof name); fn[nfn++].count = 0; }
        if (k < nfn) fn[k].count += j - i;
        i = j;
    }
    fprintf(stderr, "sampling: %d samples\n", n);
    for (int t = 0; t < 30; t++) {
        int best = -1;
        for (int k = 0; k < nfn; k++) if (fn[k].count && (best < 0 || fn[k].count > fn[best].count)) best = k;
        if (best < 0) break;
        fprintf(stderr, "  %5.1f%%  %s\n", 100.0 * fn[best].count / n, fn[best].name);
        fn[best].count = 0;
    }
}

static bool dump(const char *path, const void *p, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(p, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static int recomp(const char *snap0, const char *snap2, const char *outdir)
{
    static uint8_t r0[RAM0_SIZE], r2[RAM2_SIZE];
    FILE *a = fopen(snap0, "rb"), *b = fopen(snap2, "rb");
    bool ok = a && b && fread(r0, 1, RAM0_SIZE, a) == RAM0_SIZE && fread(r2, 1, RAM2_SIZE, b) == RAM2_SIZE;
    if (a) fclose(a);
    if (b) fclose(b);
    char err[512] = "cannot read the snapshot";
    recomp_info_t info;
    adsprec_image_t img[6];
    int nimg = sound_code_images(img, 6);
    if (ok) ok = recomp_generate(r0, r2, outdir, &info, err, sizeof err) && adsprec_generate(img, nimg, outdir, err, sizeof err);
    if (!ok) { fprintf(stderr, "recomp: %s\n", err); return 1; }
    fprintf(stderr, "recomp: %u functions, %u labels, %u files in %s\n", info.functions, info.labels, info.files, outdir);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: harness ROOT FRAMES [--ppm-every N] [--dump-at F] [--recomp R0 R2 DIR]\n"); return 2; }
    char root[512];
    snprintf(root, sizeof root, "%s%s", argv[1], argv[1][strlen(argv[1]) - 1] == '\\' ? "" : "\\");
    int frames = atoi(argv[2]), ppm_every = 0, dump_at = 0, report = 300;
    FILE *wav = NULL;
    bool play = false, play2 = false;
    bool async_audio = false;              // --async-audio: fetch sound the way the console does (no waiting)
    int ck_every = 0, ck_from = 0;         // --ck-every N [--ck-from F]: checkpoint hashes every N frames
    int play_start_until = 3000;           // --play: frames during which start is pressed
    const char *const *rec_args = NULL;
    int taps[64][3], ntaps = 0;
    int sample_from = 0, sample_to = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--ppm-every") && i + 1 < argc) ppm_every = atoi(argv[++i]);
        if (!strcmp(argv[i], "--report") && i + 1 < argc) report = atoi(argv[++i]);
        // raw mono 16-bit PCM at sound_rate() (game-derived output: keep it local)
        if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav = fopen(argv[++i], "wb");
        if (!strcmp(argv[i], "--pristine")) boot_set_pristine(true);
        // --play: press buttons on a fixed schedule (start every 2 s, a punch or
        // kick every half second), so a run goes through a fight and what follows
        if (!strcmp(argv[i], "--play")) play = true;
        if (!strcmp(argv[i], "--play2")) play = play2 = true;
        // --tap F:BIT[:P]: player P (default 1) holds input bit BIT for 4 frames from frame F
        if (!strcmp(argv[i], "--tap") && i + 1 < argc && ntaps < 64) {
            taps[ntaps][2] = 1; sscanf(argv[++i], "%d:%d:%d", &taps[ntaps][0], &taps[ntaps][1], &taps[ntaps][2]); ntaps++;
        }
        if (!strcmp(argv[i], "--async-audio")) async_audio = true;
        if (!strcmp(argv[i], "--sample") && i + 2 < argc) { sample_from = atoi(argv[++i]); sample_to = atoi(argv[++i]); }
        if (!strcmp(argv[i], "--ck-every") && i + 1 < argc) ck_every = atoi(argv[++i]);
        if (!strcmp(argv[i], "--ck-from") && i + 1 < argc) ck_from = atoi(argv[++i]);
        if (!strcmp(argv[i], "--play-start-until") && i + 1 < argc) play_start_until = atoi(argv[++i]);
        if (!strcmp(argv[i], "--hist")) cpu_interp_hist = (uint32_t *)calloc(1 << 16, sizeof *cpu_interp_hist);
        else if (!strcmp(argv[i], "--dump-at") && i + 1 < argc) dump_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--recomp") && i + 3 < argc) { rec_args = (const char *const *)&argv[i + 1]; i += 3; }
    }
    boot_set_root(root);
    char err[1024];
    if (!boot_machine(err, sizeof err)) { fprintf(stderr, "%s\n", err); return 1; }
    if (rec_args) return recomp(rec_args[0], rec_args[1], rec_args[2]);

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    double acc = 0, worst = 0;   // worst: the window's slowest frame
    int worst_f = 0;
    static int16_t snd[8192];
    uint64_t samples = 0, last_cyc = 0, last_int = 0;
    if (sample_to) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &sample_target, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
        sample_pc = (DWORD *)malloc(SAMPLE_MAX * sizeof *sample_pc);
        CreateThread(NULL, 0, sample_thread, NULL, 0, NULL);
    }
    for (int f = 1; f <= frames; f++) {
        if (sample_to) sample_on = f >= sample_from && f <= sample_to;
        QueryPerformanceCounter(&t0);
        if (play) {
            uint32_t b = 0;
            if (f > 1200 && f < play_start_until && f % 120 < 4) b |= 1u << 10;   // start (then no continues)
            if (f > 1200 && f % 30 < 3) b |= 1u << ((f / 30) % 6);       // quick/medium/fierce punch or kick
            if (play2 && f > play_start_until && f % 30 >= 3) b |= 1u << 9;   // walk right, at player 2
            for (int t = 0; t < ntaps; t++)
                if (taps[t][2] == 1 && f >= taps[t][0] && f < taps[t][0] + 4) b |= 1u << taps[t][1];
            hw.p1 = ~b;
            // --play2: player 2 joins (start) and then stands still, so player 1 wins
            uint32_t b2 = 0;
            if (play2 && f > 1260 && f < play_start_until && f % 120 >= 60 && f % 120 < 64) b2 |= 1u << 10;
            for (int t = 0; t < ntaps; t++)
                if (taps[t][2] == 2 && f >= taps[t][0] && f < taps[t][0] + 4) b2 |= 1u << taps[t][1];
            hw.p2 = ~b2;
        }
        run_frame();
        QueryPerformanceCounter(&t1);
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
        acc += ms;
        if (ms > worst) { worst = ms; worst_f = f; }
        if (checkpoint_frame((unsigned long long)f) || (ck_every && f >= ck_from && f % ck_every == 0)) {
            unsigned long long h[CK_PARTS];
            checkpoint_hashes(h);
            fprintf(stderr, "checkpoint %d: %016llx %016llx %016llx %016llx pc=%08x\n", f, h[0], h[1], h[2], h[3], cpu.pc);
        }
        int n;
        while ((n = async_audio ? sound_fetch_async(snd, 8192) : sound_fetch(snd, 8192)) > 0) {
            samples += (uint64_t)n;
            if (wav) fwrite(snd, 2, (size_t)n, wav);
        }
        if (f % report == 0) {
#ifdef UC_PROFILE
            extern double machine_sound_ms, machine_dsp_ms;
            fprintf(stderr, "  sound queueing %.2f ms/frame, DSP %.2f ms/frame\n", machine_sound_ms / report,
                    machine_dsp_ms / report);
            machine_sound_ms = machine_dsp_ms = 0;
            extern unsigned dcs_sync_count[4];
            fprintf(stderr, "  sound syncs per frame: status %.1f, command %.1f, reset %.1f, fetch %.1f\n",
                    dcs_sync_count[0] / (double)report, dcs_sync_count[1] / (double)report,
                    dcs_sync_count[2] / (double)report, dcs_sync_count[3] / (double)report);
            memset(dcs_sync_count, 0, sizeof dcs_sync_count);
#endif
#ifdef REC_PROFILE
            {   // the window's heaviest translated entry points (cycles), then start the next window
                extern uint64_t rec_prof_cycles[0x10000], rec_prof_calls[0x10000];
                uint64_t total = 0;
                for (int i = 0; i < 0x10000; i++) total += rec_prof_cycles[i];
                fprintf(stderr, "  window: %.1f M guest cycles/frame in translated code; top:", total / 1e6 / report);
                for (int t = 0; t < 5; t++) {
                    int best = 0;
                    for (int i = 1; i < 0x10000; i++) if (rec_prof_cycles[i] > rec_prof_cycles[best]) best = i;
                    if (!rec_prof_cycles[best]) break;
                    fprintf(stderr, " %08x %.0f%% (%llu calls)", 0x88000000u + best * 4, 100.0 * rec_prof_cycles[best] / (total + 1),
                            (unsigned long long)rec_prof_calls[best]);
                    rec_prof_cycles[best] = 0;
                }
                fprintf(stderr, "\n");
                extern uint64_t rec_slow_counts[4];
                fprintf(stderr, "  slow paths per frame: IDE data %.0f, other loads %.0f, stores %.0f, interpreted ops %.0f\n",
                        rec_slow_counts[0] / (double)report, rec_slow_counts[1] / (double)report,
                        rec_slow_counts[2] / (double)report, rec_slow_counts[3] / (double)report);
                memset(rec_slow_counts, 0, sizeof rec_slow_counts);
                memset(rec_prof_cycles, 0, sizeof rec_prof_cycles);
                memset(rec_prof_calls, 0, sizeof rec_prof_calls);
            }
#endif
            static uint32_t last_gen;
            fprintf(stderr, "frame %d: %.2f ms/frame (%.0f%% speed), %.0f%% interpreted, code writes %u, slowest %.2f ms at %d, pc=%08x\n",
                    f, acc / report, 100.0 * (1000.0 / FRAME_HZ) / (acc / report),
                    100.0 * (cpu_interp_count - last_int) / (double)(cpu.cycles - last_cyc + 1), rec_code_gen - last_gen,
                    worst, worst_f, cpu.pc);
            worst = 0;
            last_cyc = cpu.cycles; last_int = cpu_interp_count; last_gen = rec_code_gen;
            acc = 0;
        }
        if (ppm_every && f % ppm_every == 0) {
            char name[64];
            snprintf(name, sizeof name, "frame_%05d.ppm", f);
            save_ppm(name);
        }
        if (dump_at && f == dump_at) {
            dump("ram0.bin", ram0, RAM0_SIZE);
            dump("ram2.bin", ram2, RAM2_SIZE);
            fprintf(stderr, "dumped RAM at frame %d\n", f);
        }
    }
    if (sample_to) { sample_on = 0; sample_quit = 1; Sleep(10); sample_report(); }
    if (cpu_interp_hist) {
        // interpreted instructions per word of 0x88000000-0x8803ffff: what translation misses
        FILE *h = fopen("interp_hist.txt", "w");
        for (int i = 0; i < 1 << 16; i++)
            if (cpu_interp_hist[i] > 1000) fprintf(h, "%08x %u\n", 0x88000000u + i * 4, cpu_interp_hist[i]);
        fclose(h);
        fprintf(stderr, "translated image %08x-%08x\n", rec_image_base, rec_image_base + rec_image_words * 4);
    }
#ifdef REC_PROFILE
    {
        // guest cycles per translated entry point (0x88000000 + 4 * index)
        extern uint64_t rec_prof_cycles[0x10000], rec_prof_calls[0x10000];
        extern unsigned rec_call_depth_max;
        fprintf(stderr, "deepest nesting of direct calls: %u\n", rec_call_depth_max);
        extern unsigned long long rec_call_code_changed;
        fprintf(stderr, "calls during which code was written: %llu\n", rec_call_code_changed);
        FILE *p = fopen("rec_profile.txt", "w");
        for (int i = 0; i < 0x10000; i++)
            if (rec_prof_calls[i])
                fprintf(p, "%08x %llu %llu\n", 0x88000000u + i * 4, rec_prof_cycles[i], rec_prof_calls[i]);
        fclose(p);
        extern uint64_t rec_op_counts[1 << 17];
        p = fopen("rec_ops.txt", "w");
        for (int i = 0; i < 1 << 17; i++)
            if (rec_op_counts[i]) fprintf(p, "op %02x rs %02x fn %02x %llu\n", i >> 11, (i >> 6) & 31, i & 63,
                                          (unsigned long long)rec_op_counts[i]);
        fclose(p);
    }
#endif
    {   // the rest of the sound (with --async-audio the DSP may still have been running): wait for it now
        int n;
        while ((n = sound_fetch(snd, 8192)) > 0) {
            samples += (uint64_t)n;
            if (wav) fwrite(snd, 2, (size_t)n, wav);
        }
    }
    if (wav) { fclose(wav); fprintf(stderr, "audio: %.1f Hz mono 16-bit\n", sound_rate()); }
    save_ppm("frame_last.ppm");
    fprintf(stderr, "%llu sound samples, state hash %016llx at frame %llu\n", (unsigned long long)samples,
            (unsigned long long)state_hash(), (unsigned long long)machine_frame());
    return 0;
}
