// libretro GLSL shader presets (see shaders.h).
//
// Conventions follow RetroArch's GL driver: each pass's source holds both
// stages (#if defined(VERTEX) / FRAGMENT), sees PARAMETER_UNIFORM, and gets
// VertexCoord/TexCoord/COLOR attributes plus MVPMatrix, FrameCount,
// FrameDirection, OutputSize, TextureSize and InputSize. Besides Texture
// (its input) a pass can sample OrigTexture, PrevTexture/Prev1-6Texture
// (earlier frames), PassPrevNTexture (N passes back; the pass before the
// first is the original), PassNTexture, <alias>Texture and lookup textures
// by name, each with matching TextureSize/InputSize uniforms. Amiberry's
// names work too: Source for Texture, Original for OrigTexture, an alias by
// its plain name, and OrigTextureSize without an Orig sampler.
//
// Inside the chain an image's top row is at texture y = 0; the last pass
// flips to the screen.
#include "shaders.h"
#include "gl_funcs.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_FAILURE_USERMSG
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "third_party/stb_image.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define MAX_PASSES 32
#define MAX_LUTS 16
#define MAX_PARAMS 160
#define MAX_BINDS 48
#define HISTORY 8
#define SRC_W 320
#define SRC_H 240

enum { SCALE_SOURCE, SCALE_VIEWPORT, SCALE_ABSOLUTE };
enum { SRC_ORIG = -1, SRC_HIST = -100, SRC_LUT = 1000 };   // SRC_HIST - k: k frames back

typedef struct {
    GLint tex, tex_size, in_size;
    int unit, src;
} bind_t;

typedef struct {
    GLuint prog;
    int scale_type[2];
    float scale[2];
    bool has_scale, linear, float_fbo, srgb_fbo, mipmap;
    GLint wrap;
    int frame_mod;
    char alias[64];
    GLuint fbo, tex;
    int w, h;
    GLint u_mvp, u_count, u_dir, u_out, u_texsize, u_insize, u_texture, u_origsize, u_originsize;
    bind_t bind[MAX_BINDS];
    int nbind;
    GLint u_param[MAX_PARAMS];
} pass_t;

typedef struct {
    char name[64];
    GLuint tex;
    int w, h;
    bool linear;
} lut_t;

static pass_t pass[MAX_PASSES];
static int npass;
static lut_t lut[MAX_LUTS];
static int nlut;
static shader_param_t params[MAX_PARAMS];
static int nparams;
static char current[1024];
static GLuint hist[HISTORY];
static int hist_pos;
static unsigned frame_count;
static GLuint stock_prog;           // final copy when the last pass has its own size
static bool final_linear = true;

/* ---------------------------------------------------------------- */
/* Preset files                                                     */
/* ---------------------------------------------------------------- */

typedef struct { char key[96], val[512], dir[1024]; } kv_t;
static kv_t *kv;
static int nkv;

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void dir_of(const char *path, char *dir, size_t n)
{
    snprintf(dir, n, "%s", path);
    char *s = strrchr(dir, '/'), *b = strrchr(dir, '\\');
    if (b > s) s = b;
    if (s) s[1] = 0; else dir[0] = 0;
}

static char *read_file(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) buf[n] = 0;
    if (len) *len = n;
    return buf;
}

static void add_kv(const char *k, char *v, const char *dir)
{
    if (nkv >= 1024) return;
    if (*v == '"') {                          // "quoted" value, maybe followed by a comment
        v++;
        char *q = strchr(v, '"');
        if (q) *q = 0;
    } else {                                  // bare value: a trailing # or // comment ends it
        char *c = strstr(v, "#"), *c2 = strstr(v, "//");
        if (c2 && (!c || c2 < c)) c = c2;
        if (c) *c = 0;
        v = trim(v);
    }
    snprintf(kv[nkv].key, sizeof kv[nkv].key, "%s", k);
    snprintf(kv[nkv].val, sizeof kv[nkv].val, "%s", v);
    snprintf(kv[nkv].dir, sizeof kv[nkv].dir, "%s", dir);
    nkv++;
}

// key = value lines; "#reference "file"" pulls another preset in at that
// point, so lines after it override what it set.
static bool parse_preset(const char *path, int depth)
{
    char *text = read_file(path, NULL);
    if (!text || depth > 8) { free(text); return false; }
    char dir[1024];
    dir_of(path, dir, sizeof dir);
    for (char *line = text; line && *line;) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        char *s = trim(line);
        if (!strncmp(s, "#reference", 10)) {
            char *q = strchr(s, '"');
            if (q) {
                char ref[1024], full[2048];
                snprintf(ref, sizeof ref, "%s", q + 1);
                char *e = strchr(ref, '"');
                if (e) *e = 0;
                snprintf(full, sizeof full, "%s%s", dir, ref);
                parse_preset(full, depth + 1);
            }
        } else if (*s && *s != '#' && strncmp(s, "//", 2)) {
            char *eq = strchr(s, '=');
            if (eq) { *eq = 0; add_kv(trim(s), trim(eq + 1), dir); }
        }
        line = nl ? nl + 1 : NULL;
    }
    free(text);
    return true;
}

static const kv_t *get(const char *fmt, ...)
{
    char key[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(key, sizeof key, fmt, ap);
    va_end(ap);
    for (int i = nkv - 1; i >= 0; i--)        // later lines (and referencing files) win
        if (!strcmp(kv[i].key, key)) return &kv[i];
    return NULL;
}

static bool get_bool(bool def, const char *fmt, int i)
{
    const kv_t *e = get(fmt, i);
    if (!e) return def;
    return !strcmp(e->val, "true") || !strcmp(e->val, "1");
}

static float get_float(float def, const char *fmt, int i)
{
    const kv_t *e = get(fmt, i);
    return e ? (float)atof(e->val) : def;
}

static int scale_type(const char *v)
{
    if (!strcmp(v, "viewport")) return SCALE_VIEWPORT;
    if (!strcmp(v, "absolute")) return SCALE_ABSOLUTE;
    return SCALE_SOURCE;
}

static GLint wrap_mode(const char *v)
{
    if (!v) return GL_CLAMP_TO_BORDER;
    if (!strcmp(v, "repeat")) return GL_REPEAT;
    if (!strcmp(v, "mirrored_repeat")) return GL_MIRRORED_REPEAT;
    if (!strcmp(v, "clamp_to_edge")) return GL_CLAMP_TO_EDGE;
    return GL_CLAMP_TO_BORDER;
}

/* ---------------------------------------------------------------- */
/* Compiling                                                        */
/* ---------------------------------------------------------------- */

static int glsl_version(void)
{
    static int v = -1;
    if (v < 0) {
        const char *s = (const char *)p_glGetString(0x8B8C /* GL_SHADING_LANGUAGE_VERSION */);
        int maj = 1, min = 10;
        if (s) sscanf(s, "%d.%d", &maj, &min);
        v = maj * 100 + min;
    }
    return v;
}

static void collect_params(const char *src)
{
    for (const char *p = strstr(src, "#pragma parameter"); p; p = strstr(p + 1, "#pragma parameter")) {
        char name[64], desc[96] = "";
        float def = 0, mn = 0, mx = 1, st = 0;
        const char *q = p + 17;
        if (sscanf(q, " %63s", name) != 1) continue;
        const char *d0 = strchr(q, '"'), *nl = strpbrk(q, "\r\n");
        if (!d0 || (nl && d0 > nl)) continue;
        const char *d1 = strchr(d0 + 1, '"');
        if (!d1) continue;
        snprintf(desc, sizeof desc, "%.*s", (int)(d1 - d0 - 1), d0 + 1);
        int n = sscanf(d1 + 1, "%f %f %f %f", &def, &mn, &mx, &st);
        if (n < 3) continue;
        bool seen = false;
        for (int i = 0; i < nparams; i++) if (!strcmp(params[i].name, name)) seen = true;
        if (seen || nparams >= MAX_PARAMS) continue;
        shader_param_t *pp = &params[nparams++];
        snprintf(pp->name, sizeof pp->name, "%s", name);
        snprintf(pp->desc, sizeof pp->desc, "%s", *trim(desc) ? desc : name);
        pp->def = pp->value = def; pp->min = mn; pp->max = mx; pp->step = n >= 4 && st > 0 ? st : (mx - mn) / 20;
    }
}

static GLuint compile_stage(GLenum type, const char *src, const char *path, char *err, size_t errlen)
{
    // a #version line in the file must come first; otherwise pick one
    char version[64];
    const char *vl = strstr(src, "#version");
    if (vl) {
        const char *e = strpbrk(vl, "\r\n");
        snprintf(version, sizeof version, "%.*s\n", (int)(e ? e - vl : (long)strlen(vl)), vl);
    } else snprintf(version, sizeof version, "#version %d\n", glsl_version() >= 130 ? 130 : 110);
    char *body = strdup(src);
    if (vl) { char *b = body + (vl - src); while (*b && *b != '\n' && *b != '\r') *b++ = ' '; }
    const char *defs = type == GL_VERTEX_SHADER ? "#define VERTEX\n#define PARAMETER_UNIFORM\n" : "#define FRAGMENT\n#define PARAMETER_UNIFORM\n";
    const char *parts[3] = { version, defs, body };
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 3, parts, NULL);
    p_glCompileShader(s);
    free(body);
    GLint ok;
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        p_glGetShaderInfoLog(s, sizeof log, NULL, log);
        snprintf(err, errlen, "%s (%s): %s", path, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        p_glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint build_program(const char *src, const char *path, char *err, size_t errlen)
{
    GLuint vs = compile_stage(GL_VERTEX_SHADER, src, path, err, errlen);
    if (!vs) return 0;
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, src, path, err, errlen);
    if (!fs) { p_glDeleteShader(vs); return 0; }
    GLuint p = p_glCreateProgram();
    p_glAttachShader(p, vs);
    p_glAttachShader(p, fs);
    p_glBindAttribLocation(p, 0, "VertexCoord");
    p_glBindAttribLocation(p, 1, "TexCoord");
    p_glBindAttribLocation(p, 2, "COLOR");
    p_glLinkProgram(p);
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);
    GLint ok;
    p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        p_glGetProgramInfoLog(p, sizeof log, NULL, log);
        snprintf(err, errlen, "%s: link: %s", path, log);
        p_glDeleteProgram(p);
        return 0;
    }
    return p;
}

static void add_bind(pass_t *ps, const char *base, int src)
{
    char n[160];
    snprintf(n, sizeof n, "%sTexture", base);
    GLint t = p_glGetUniformLocation(ps->prog, n);
    if (t < 0 && src >= 0) t = p_glGetUniformLocation(ps->prog, base);   // lookup textures and aliases by plain name
    if (t < 0 && !strcmp(base, "Orig")) t = p_glGetUniformLocation(ps->prog, "Original");
    if (t < 0 || ps->nbind >= MAX_BINDS) return;
    bind_t *b = &ps->bind[ps->nbind];
    b->tex = t;
    snprintf(n, sizeof n, "%sTextureSize", base); b->tex_size = p_glGetUniformLocation(ps->prog, n);
    snprintf(n, sizeof n, "%sInputSize", base); b->in_size = p_glGetUniformLocation(ps->prog, n);
    b->src = src;
    b->unit = ps->nbind + 1;                  // unit 0 is Texture
    ps->nbind++;
}

static void find_uniforms(int i)
{
    pass_t *ps = &pass[i];
    GLuint p = ps->prog;
    ps->u_mvp = p_glGetUniformLocation(p, "MVPMatrix");
    ps->u_count = p_glGetUniformLocation(p, "FrameCount");
    ps->u_dir = p_glGetUniformLocation(p, "FrameDirection");
    ps->u_out = p_glGetUniformLocation(p, "OutputSize");
    ps->u_texsize = p_glGetUniformLocation(p, "TextureSize");
    ps->u_insize = p_glGetUniformLocation(p, "InputSize");
    ps->u_texture = p_glGetUniformLocation(p, "Texture");
    if (ps->u_texture < 0) ps->u_texture = p_glGetUniformLocation(p, "Source");
    ps->u_origsize = p_glGetUniformLocation(p, "OrigTextureSize");
    ps->u_originsize = p_glGetUniformLocation(p, "OrigInputSize");
    ps->nbind = 0;
    add_bind(ps, "Orig", SRC_ORIG);
    add_bind(ps, "Prev", SRC_HIST - 1);
    for (int k = 1; k < HISTORY - 1; k++) { char n[16]; snprintf(n, sizeof n, "Prev%d", k); add_bind(ps, n, SRC_HIST - 1 - k); }
    for (int k = 1; k <= i + 1; k++) { char n[24]; snprintf(n, sizeof n, "PassPrev%d", k); add_bind(ps, n, i - k); }
    for (int k = 1; k <= i; k++) { char n[24]; snprintf(n, sizeof n, "Pass%d", k); add_bind(ps, n, k - 1); }
    for (int j = 0; j < i; j++) if (pass[j].alias[0]) add_bind(ps, pass[j].alias, j);
    for (int l = 0; l < nlut; l++) add_bind(ps, lut[l].name, SRC_LUT + l);
    for (int k = 0; k < nparams; k++) ps->u_param[k] = p_glGetUniformLocation(p, params[k].name);
}

/* ---------------------------------------------------------------- */
/* Textures and framebuffers                                        */
/* ---------------------------------------------------------------- */

static GLuint new_texture(int w, int h, GLint fmt, GLenum type)
{
    GLuint t;
    p_glGenTextures(1, &t);
    p_glBindTexture(GL_TEXTURE_2D, t);
    p_glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, type, NULL);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

static bool load_lut(lut_t *l, const char *path, bool linear, bool mipmap, GLint wrap, char *err, size_t errlen)
{
    int w, h, n;
    unsigned char *img = stbi_load(path, &w, &h, &n, 4);
    if (!img) { snprintf(err, errlen, "%s: %s", path, stbi_failure_reason()); return false; }
    p_glGenTextures(1, &l->tex);
    p_glBindTexture(GL_TEXTURE_2D, l->tex);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    p_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    stbi_image_free(img);
    if (mipmap) p_glGenerateMipmap(GL_TEXTURE_2D);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmap ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : (linear ? GL_LINEAR : GL_NEAREST));
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    l->w = w; l->h = h; l->linear = linear;
    return true;
}

static void ensure_fbo(pass_t *ps, int w, int h)
{
    if (ps->fbo && ps->w == w && ps->h == h) return;
    if (ps->fbo) { p_glDeleteFramebuffers(1, &ps->fbo); p_glDeleteTextures(1, &ps->tex); }
    GLint fmt = ps->float_fbo ? GL_RGBA16F : ps->srgb_fbo ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    ps->tex = new_texture(w, h, fmt, ps->float_fbo ? GL_FLOAT : GL_UNSIGNED_BYTE);
    p_glGenFramebuffers(1, &ps->fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, ps->fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ps->tex, 0);
    p_glClearColor(0, 0, 0, 0);
    p_glClear(GL_COLOR_BUFFER_BIT);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ps->w = w; ps->h = h;
}

/* ---------------------------------------------------------------- */

void shader_unload(void)
{
    for (int i = 0; i < npass; i++) {
        if (pass[i].prog) p_glDeleteProgram(pass[i].prog);
        if (pass[i].fbo) { p_glDeleteFramebuffers(1, &pass[i].fbo); p_glDeleteTextures(1, &pass[i].tex); }
    }
    for (int i = 0; i < nlut; i++) p_glDeleteTextures(1, &lut[i].tex);
    memset(pass, 0, sizeof pass);
    npass = nlut = nparams = 0;
    current[0] = 0;
}

bool shader_active(void) { return npass > 0; }
const char *shader_path(void) { return current; }
int shader_param_count(void) { return nparams; }
shader_param_t *shader_param(int i) { return i >= 0 && i < nparams ? &params[i] : NULL; }

static const char *stock_src =
    "#if defined(VERTEX)\n"
    "#if __VERSION__ >= 130\n#define V out\n#define A in\n#else\n#define V varying\n#define A attribute\n#endif\n"
    "A vec4 VertexCoord; A vec4 TexCoord; V vec2 uv; uniform mat4 MVPMatrix;\n"
    "void main() { gl_Position = MVPMatrix * VertexCoord; uv = TexCoord.xy; }\n"
    "#else\n"
    "#if __VERSION__ >= 130\n#define V in\nout vec4 FragColor;\n#define T texture\n#else\n#define V varying\n#define FragColor gl_FragColor\n#define T texture2D\n#endif\n"
    "V vec2 uv; uniform sampler2D Texture;\n"
    "void main() { FragColor = T(Texture, uv); }\n"
    "#endif\n";

bool shader_load(const char *path, char *err, size_t errlen)
{
    // build into a fresh state; restore the old preset if anything fails
    char old[1024];
    snprintf(old, sizeof old, "%s", current);
    shader_unload();
    if (!kv) kv = malloc(sizeof(kv_t) * 1024);
    nkv = 0;
    if (!parse_preset(path, 0)) { snprintf(err, errlen, "%s: cannot read", path); goto fail; }
    const kv_t *e = get("shaders");
    int n = e ? atoi(e->val) : 0;
    if (n < 1 || n > MAX_PASSES) { snprintf(err, errlen, "%s: no shaders", path); goto fail; }

    // lookup textures
    e = get("textures");
    if (e) {
        char list[512];
        snprintf(list, sizeof list, "%s", e->val);
        for (char *name = strtok(list, ";"); name && nlut < MAX_LUTS; name = strtok(NULL, ";")) {
            name = trim(name);
            const kv_t *f = get("%s", name);
            if (!f) continue;
            char full[2048], key[96];
            snprintf(full, sizeof full, "%s%s", f->dir, f->val);
            snprintf(key, sizeof key, "%s_linear", name);
            const kv_t *lin = get("%s", key);
            snprintf(key, sizeof key, "%s_mipmap", name);
            const kv_t *mip = get("%s", key);
            snprintf(key, sizeof key, "%s_wrap_mode", name);
            const kv_t *wr = get("%s", key);
            lut_t *l = &lut[nlut];
            snprintf(l->name, sizeof l->name, "%s", name);
            if (!load_lut(l, full, lin && !strcmp(lin->val, "true"), mip && !strcmp(mip->val, "true"), wrap_mode(wr ? wr->val : NULL), err, errlen)) goto fail;
            nlut++;
        }
    }

    // passes: settings and parameters first, then compile
    char *srcs[MAX_PASSES] = { 0 };
    for (int i = 0; i < n; i++) {
        pass_t *ps = &pass[i];
        e = get("shader%d", i);
        if (!e) { snprintf(err, errlen, "%s: shader%d missing", path, i); goto fail_src; }
        char full[2048];
        snprintf(full, sizeof full, "%s%s", e->dir, e->val);
        srcs[i] = read_file(full, NULL);
        if (!srcs[i]) { snprintf(err, errlen, "%s: cannot read", full); goto fail_src; }
        collect_params(srcs[i]);
        ps->linear = get_bool(false, "filter_linear%d", i);
        ps->float_fbo = get_bool(false, "float_framebuffer%d", i);
        ps->srgb_fbo = get_bool(false, "srgb_framebuffer%d", i);
        ps->mipmap = get_bool(false, "mipmap_input%d", i);
        const kv_t *w = get("wrap_mode%d", i);
        ps->wrap = wrap_mode(w ? w->val : NULL);
        ps->frame_mod = (int)get_float(0, "frame_count_mod%d", i);
        const kv_t *al = get("alias%d", i);
        if (al) snprintf(ps->alias, sizeof ps->alias, "%s", al->val);
        const kv_t *st = get("scale_type%d", i), *sx = get("scale_type_x%d", i), *sy = get("scale_type_y%d", i);
        ps->has_scale = st || sx || sy;
        ps->scale_type[0] = scale_type(sx ? sx->val : st ? st->val : "source");
        ps->scale_type[1] = scale_type(sy ? sy->val : st ? st->val : "source");
        float s = get_float(1, "scale%d", i);
        ps->scale[0] = get_float(s, "scale_x%d", i);
        ps->scale[1] = get_float(s, "scale_y%d", i);
    }
    final_linear = get_bool(true, "filter_linear%d", n);
    // parameter values set by the preset
    e = get("parameters");
    if (e) {
        char list[1024];
        snprintf(list, sizeof list, "%s", e->val);
        for (char *name = strtok(list, ";"); name; name = strtok(NULL, ";")) {
            name = trim(name);
            const kv_t *v = get("%s", name);
            for (int k = 0; v && k < nparams; k++)
                if (!strcmp(params[k].name, name)) params[k].def = params[k].value = (float)atof(v->val);
        }
    }
    for (int i = 0; i < n; i++) {
        char full[2048];
        const kv_t *sh = get("shader%d", i);
        snprintf(full, sizeof full, "%s%s", sh->dir, sh->val);
        pass[i].prog = build_program(srcs[i], full, err, errlen);
        if (!pass[i].prog) { npass = i; goto fail_src; }
        npass = i + 1;
    }
    for (int i = 0; i < n; i++) { free(srcs[i]); find_uniforms(i); }

    if (!hist[0]) for (int i = 0; i < HISTORY; i++) {
        hist[i] = new_texture(SRC_W, SRC_H, GL_RGBA8, GL_UNSIGNED_BYTE);
        static uint32_t black[SRC_W * SRC_H];
        p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SRC_W, SRC_H, GL_BGRA, GL_UNSIGNED_BYTE, black);
    }
    if (!stock_prog) stock_prog = build_program(stock_src, "stock", err, errlen);
    snprintf(current, sizeof current, "%s", path);
    frame_count = 0;
    return true;

fail_src:
    for (int i = 0; i < MAX_PASSES; i++) free(srcs[i]);
fail:
    shader_unload();
    if (old[0] && strcmp(old, path)) {
        char e2[256];
        shader_load(old, e2, sizeof e2);
    }
    return false;
}

static void bind_texture(int unit, GLuint tex, bool linear, GLint wrap, bool mipmap)
{
    p_glActiveTexture(GL_TEXTURE0 + unit);
    p_glBindTexture(GL_TEXTURE_2D, tex);
    if (mipmap) p_glGenerateMipmap(GL_TEXTURE_2D);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      mipmap ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : (linear ? GL_LINEAR : GL_NEAREST));
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    if (wrap) {
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    }
}

static void draw_quad(bool to_screen)
{
    static const GLfloat vert[] = { 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1 };
    static const GLfloat tex_fbo[] = { 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0 };
    static const GLfloat tex_screen[] = { 0, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
    p_glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, vert);
    p_glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, to_screen ? tex_screen : tex_fbo);
    p_glEnableVertexAttribArray(0);
    p_glEnableVertexAttribArray(1);
    p_glDisableVertexAttribArray(2);
    p_glVertexAttrib4f(2, 1, 1, 1, 1);
    p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static const GLfloat mvp[16] = { 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1 };

void shader_render(const uint32_t *px, SDL_Rect vp, int drawable_h)
{
    if (!npass) return;
    hist_pos = (hist_pos + 1) % HISTORY;
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindTexture(GL_TEXTURE_2D, hist[hist_pos]);
    p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, SRC_W, SRC_H, GL_BGRA, GL_UNSIGNED_BYTE, px);
    frame_count++;
    p_glDisable(GL_BLEND);

    GLuint in_tex = hist[hist_pos];
    int in_w = SRC_W, in_h = SRC_H;
    bool to_screen = false;
    for (int i = 0; i < npass; i++) {
        pass_t *ps = &pass[i];
        to_screen = i == npass - 1 && !ps->has_scale;
        int w = vp.w, h = vp.h;
        if (!to_screen) {
            float dim[2] = { 0, 0 }, src[2] = { (float)in_w, (float)in_h }, view[2] = { (float)vp.w, (float)vp.h };
            for (int a = 0; a < 2; a++)
                dim[a] = ps->scale_type[a] == SCALE_VIEWPORT ? view[a] * ps->scale[a] :
                         ps->scale_type[a] == SCALE_ABSOLUTE ? ps->scale[a] : src[a] * ps->scale[a];
            w = (int)(dim[0] + 0.5f); h = (int)(dim[1] + 0.5f);
            if (w < 1) w = 1;
            if (h < 1) h = 1;
            if (w > 8192) w = 8192;
            if (h > 8192) h = 8192;
            ensure_fbo(ps, w, h);
            p_glBindFramebuffer(GL_FRAMEBUFFER, ps->fbo);
            p_glViewport(0, 0, w, h);
        } else {
            p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
            p_glViewport(vp.x, drawable_h - vp.y - vp.h, vp.w, vp.h);
        }
        if (ps->srgb_fbo && !to_screen) p_glEnable(GL_FRAMEBUFFER_SRGB);
        else p_glDisable(GL_FRAMEBUFFER_SRGB);

        p_glUseProgram(ps->prog);
        if (ps->u_mvp >= 0) p_glUniformMatrix4fv(ps->u_mvp, 1, GL_FALSE, mvp);
        if (ps->u_count >= 0) p_glUniform1i(ps->u_count, (GLint)(ps->frame_mod ? frame_count % (unsigned)ps->frame_mod : frame_count));
        if (ps->u_dir >= 0) p_glUniform1i(ps->u_dir, 1);
        if (ps->u_out >= 0) p_glUniform2f(ps->u_out, (GLfloat)w, (GLfloat)h);
        if (ps->u_texsize >= 0) p_glUniform2f(ps->u_texsize, (GLfloat)in_w, (GLfloat)in_h);
        if (ps->u_insize >= 0) p_glUniform2f(ps->u_insize, (GLfloat)in_w, (GLfloat)in_h);
        if (ps->u_origsize >= 0) p_glUniform2f(ps->u_origsize, SRC_W, SRC_H);
        if (ps->u_originsize >= 0) p_glUniform2f(ps->u_originsize, SRC_W, SRC_H);
        for (int k = 0; k < nparams; k++) if (ps->u_param[k] >= 0) p_glUniform1f(ps->u_param[k], params[k].value);

        for (int b = 0; b < ps->nbind; b++) {
            bind_t *bd = &ps->bind[b];
            GLuint t; int tw, th; bool lin;
            if (bd->src >= SRC_LUT) { lut_t *l = &lut[bd->src - SRC_LUT]; t = l->tex; tw = l->w; th = l->h; lin = l->linear;
                p_glActiveTexture(GL_TEXTURE0 + bd->unit); p_glBindTexture(GL_TEXTURE_2D, t); }
            else {
                if (bd->src <= SRC_HIST) t = hist[(hist_pos - (SRC_HIST - bd->src) + HISTORY * 2) % HISTORY], tw = SRC_W, th = SRC_H, lin = pass[0].linear;
                else if (bd->src == SRC_ORIG) t = hist[hist_pos], tw = SRC_W, th = SRC_H, lin = pass[0].linear;
                else { t = pass[bd->src].tex; tw = pass[bd->src].w; th = pass[bd->src].h; lin = pass[bd->src + 1].linear; }
                bind_texture(bd->unit, t, lin, 0, false);
            }
            p_glUniform1i(bd->tex, bd->unit);
            if (bd->tex_size >= 0) p_glUniform2f(bd->tex_size, (GLfloat)tw, (GLfloat)th);
            if (bd->in_size >= 0) p_glUniform2f(bd->in_size, (GLfloat)tw, (GLfloat)th);
        }
        bind_texture(0, in_tex, ps->linear, ps->wrap, ps->mipmap);
        if (ps->u_texture >= 0) p_glUniform1i(ps->u_texture, 0);
        draw_quad(to_screen);

        if (!to_screen) { in_tex = ps->tex; in_w = ps->w; in_h = ps->h; }
    }
    p_glDisable(GL_FRAMEBUFFER_SRGB);
    if (!to_screen) {
        // the last pass had its own size: scale its output to the screen
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        p_glViewport(vp.x, drawable_h - vp.y - vp.h, vp.w, vp.h);
        p_glUseProgram(stock_prog);
        p_glUniformMatrix4fv(p_glGetUniformLocation(stock_prog, "MVPMatrix"), 1, GL_FALSE, mvp);
        p_glUniform1i(p_glGetUniformLocation(stock_prog, "Texture"), 0);
        bind_texture(0, in_tex, final_linear, GL_CLAMP_TO_EDGE, false);
        draw_quad(true);
    }
    p_glActiveTexture(GL_TEXTURE0);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/* ---------------------------------------------------------------- */

void shader_params_save(char *buf, size_t n)
{
    size_t len = 0;
    buf[0] = 0;
    for (int i = 0; i < nparams && len + 80 < n; i++)
        if (params[i].value != params[i].def)
            len += (size_t)snprintf(buf + len, n - len, "%s%s=%g", len ? ";" : "", params[i].name, params[i].value);
}

void shader_params_apply(const char *s)
{
    char tmp[2048];
    snprintf(tmp, sizeof tmp, "%s", s);
    for (char *t = strtok(tmp, ";"); t; t = strtok(NULL, ";")) {
        char *eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        for (int i = 0; i < nparams; i++)
            if (!strcmp(params[i].name, trim(t))) params[i].value = (float)atof(eq + 1);
    }
}

static int cmp_names(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static void walk(const char *root, const char *rel, char names[][192], int *n, int max, int depth)
{
    char path[2048];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    if (depth > 6) return;
#ifdef _WIN32
    char pattern[2100];
    snprintf(pattern, sizeof pattern, "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char *name = fd.cFileName;
        bool is_dir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
#else
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *name = de->d_name;
        char full[2300];
        snprintf(full, sizeof full, "%s/%s", path, name);
        struct stat st;
        bool is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
#endif
        if (name[0] == '.') continue;
        char sub[1024];
        snprintf(sub, sizeof sub, "%s%s%s", rel, *rel ? "/" : "", name);
        size_t l = strlen(name);
        if (is_dir) walk(root, sub, names, n, max, depth + 1);
        else if (l > 6 && !strcmp(name + l - 6, ".glslp") && *n < max) snprintf(names[(*n)++], 192, "%s", sub);
#ifdef _WIN32
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    }
    closedir(d);
#endif
}

int shader_list(const char *dir, char names[][192], int max)
{
    int n = 0;
    walk(dir, "", names, &n, max, 0);
    qsort(names, (size_t)n, sizeof names[0], cmp_names);
    return n;
}
