#include "../NDS.h"
#include "../GPU.h"
#include "../version.h"
#include "../Config.h"
#include "../OpenGLSupport.h"
#include "../SPU.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dirent.h>
#define _USE_MATH_DEFINES
#include <math.h>

#include <unordered_map>
#include <algorithm>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"

#include "dr_wav.h"

#include "compat_switch.h"

#include "profiler.h"

#ifdef GDB_ENABLED
#include <gdbstub.h>
#endif

static EGLDisplay eglDisplay;
static EGLContext eglCtx;
static EGLSurface eglSurface;

extern std::unordered_map<u32, u32> arm9BlockFrequency;
extern std::unordered_map<u32, u32> arm7BlockFrequency;

namespace Config
{

int ScreenRotation;
int ScreenGap;
int ScreenLayout;
int ScreenSizing;

int IntegerScaling;

int Filtering;

char LastROMFolder[512];

int SwitchOverclock;

int DirectBoot;

int GlobalRotation;

ConfigEntry PlatformConfigFile[] =
{
    {"ScreenRotation", 0, &ScreenRotation, 0, NULL, 0},
    {"ScreenGap",      0, &ScreenGap,      0, NULL, 0},
    {"ScreenLayout",   0, &ScreenLayout,   0, NULL, 0},
    {"ScreenSizing",   0, &ScreenSizing,   0, NULL, 0},
    {"Filtering",      0, &Filtering,      1, NULL, 0},
    {"IntegerScaling", 0, &IntegerScaling, 0, NULL, 0},
    {"GlobalRotation", 0, &GlobalRotation, 0, NULL, 0},

    {"LastROMFolder", 1, LastROMFolder, 0, (char*)"/", 511},

    {"SwitchOverclock", 0, &SwitchOverclock, 0, NULL, 0},

    {"DirectBoot",   0, &DirectBoot,     1, NULL, 0},

    {"", -1, NULL, 0, NULL, 0}
};
}

void InitEGL(NWindow* window)
{
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    
    eglInitialize(eglDisplay, NULL, NULL);

    eglBindAPI(EGL_OPENGL_API);

    EGLConfig config;
    EGLint numConfigs;
    static const EGLint framebufferAttributeList[] =
    {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE,     8,
        EGL_GREEN_SIZE,   8,
        EGL_BLUE_SIZE,    8,
        EGL_ALPHA_SIZE,   8,
        EGL_DEPTH_SIZE,   24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    eglChooseConfig(eglDisplay, framebufferAttributeList, &config, 1, &numConfigs);

    eglSurface = eglCreateWindowSurface(eglDisplay, config, window, NULL);

    static const EGLint contextAttributeList[] =
    {
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
        EGL_CONTEXT_MINOR_VERSION_KHR, 3,
        EGL_NONE
    };
    eglCtx = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, contextAttributeList);

    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglCtx);
}

void DeInitEGL()
{
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(eglDisplay, eglCtx);
    eglDestroySurface(eglDisplay, eglSurface);
    eglTerminate(eglDisplay);
}

void applyOverclock(bool usePCV, ClkrstSession* session, int setting)
{
    const int clockSpeeds[] = { 1020000000, 1224000000, 1581000000, 1785000000 };
    if (usePCV)
        pcvSetClockRate(PcvModule_CpuBus, clockSpeeds[setting]);
    else
        clkrstSetClockRate(session, clockSpeeds[setting]);
}

// those matrix functions are copied from https://github.com/vurtun/mmx/blob/master/vec.h
// distributed under the zlib license: https://github.com/vurtun/mmx/blob/master/vec.h#L68
#define xv_zero_array(p,n) xv_zero_size(p, (n) * (int)sizeof((p)[0]))
void xv_zero_size(void *ptr, int size)
{
    memset(ptr, 0, size);
}
void
xm4_orthographic(float *m, float left, float right, float bottom, float top,
    float near, float far)
{
    #define M(col, row) m[(col<<2)+row]
    xv_zero_array(m, 16);
    M(0,0) = 2.0f/(right-left);
    M(1,1) = 2.0f/(top-bottom);
    M(2,2) = -2.0f/(far - near);
    M(3,0) = -(right+left)/(right-left);
    M(3,1) = -(top+bottom)/(top-bottom);
    M(3,2) = -(far+near)/(far-near);
    M(3,3) = 1.0f;
    #undef M
}

#define MMX_MEMCPY memcpy
#define MMX_SIN sin
#define MMX_COS cos

void
xm4_mul(float *product, const float *m1, const float *m2)
{
    int i;
    float a[16], b[16], o[16];
    #define A(col, row) a[(col << 2)+row]
    #define B(col, row) b[(col << 2)+row]
    #define P(col, row) o[(col << 2)+row]

    /* load */
    MMX_MEMCPY(a, m1, sizeof(a));
    MMX_MEMCPY(b, m2, sizeof(b));

    /* calculate */
    for (i = 0; i < 4; ++i) {
        const float ai0 = A(i,0), ai1 = A(i,1), ai2 = A(i,2), ai3 = A(i,3);
        P(i,0) = ai0 * B(0,0) + ai1 * B(1,0) + ai2 * B(2,0) + ai3 * B(3,0);
        P(i,1) = ai0 * B(0,1) + ai1 * B(1,1) + ai2 * B(2,1) + ai3 * B(3,1);
        P(i,2) = ai0 * B(0,2) + ai1 * B(1,2) + ai2 * B(2,2) + ai3 * B(3,2);
        P(i,3) = ai0 * B(0,3) + ai1 * B(1,3) + ai2 * B(2,3) + ai3 * B(3,3);
    }

    /* store */
    MMX_MEMCPY(product, o, sizeof(o));
    #undef A
    #undef B
    #undef P
}

void
xm3_rotate(float *m, float angle, float X, float Y, float Z)
{
    #define M(col, row) m[(col*3)+row]
#ifdef MMX_USE_DEGREES
    float s = (float)MMX_SIN(MMX_DEG2RAD(angle));
    float c = (float)MMX_COS(MMX_DEG2RAD(angle));
#else
    float s = (float)MMX_SIN(angle);
    float c = (float)MMX_COS(angle);
#endif
    float oc = 1.0f - c;
    M(0,0) = oc * X * X + c;
    M(0,1) = oc * X * Y - Z * s;
    M(0,2) = oc * Z * X + Y * s;

    M(1,0) = oc * X * Y + Z * s;
    M(1,1) = oc * Y * Y + c;
    M(1,2) = oc * Y * Z - X * s;

    M(2,0) = oc * Z * X - Y * s;
    M(2,1) = oc * Y * Z + X * s;
    M(2,2) = oc * Z * Z + c;
    #undef M
}

void
xm4_from_mat3(float *r, const float *m)
{
    #define M(col, row) r[(col<<2)+row]
    #define T(col, row) m[(col*3)+row]
    M(0,0) = T(0,0); M(0,1) = T(0,1); M(0,2) = T(0,2); M(0,3) = 0;
    M(1,0) = T(1,0); M(1,1) = T(1,1); M(1,2) = T(1,2); M(1,3) = 0;
    M(2,0) = T(2,0); M(2,1) = T(2,1); M(2,2) = T(2,2); M(2,3) = 0;
    M(3,0) = 0; M(3,1) = 0; M(3,2) = 0; M(3,3) = 1;
    #undef M
    #undef T
}

void
xm4_rotatef(float *m, float angle, float X, float Y, float Z)
{
    float t[9];
    xm3_rotate(t, angle, X, Y, Z);
    xm4_from_mat3(m, t);
}

#define xv2_cpy(to,from)    (to)[0]=(from)[0], (to)[1]=(from)[1]
void
xm4_scalev(float *m, float x, float y, float z)
{
    #define M(col, row) m[(col<<2)+row]
    xv_zero_array(m, 16);
    M(0,0) = x;
    M(1,1) = y;
    M(2,2) = z;
    M(3,3) = 1.0f;
    #undef M
}

void
xm2_mul(float *product, const float *m1, const float *m2)
{
    #define A(col, row) a[(col<<1)+row]
    #define B(col, row) b[(col<<1)+row]
    #define P(col, row) o[(col<<1)+row]

    /* load */
    float a[4], b[4], o[4];
    MMX_MEMCPY(a, m1, sizeof(a));
    MMX_MEMCPY(b, m2, sizeof(b));

    /* calculate */
    P(0,0) = A(0,0) * B(0,0) + A(0,1) * B(1,0);
    P(0,1) = A(0,0) * B(0,1) + A(0,1) * B(1,1);
    P(1,0) = A(1,0) * B(0,0) + A(1,1) * B(1,0);
    P(1,1) = A(1,0) * B(0,1) + A(1,1) * B(1,1);

    /* store */
    MMX_MEMCPY(product, o, sizeof(o));

    #undef A
    #undef B
    #undef P
}


void
xm2_transform(float *r, const float *m, const float *vec)
{
    float v[2], o[2];
    #define X(a) a[0]
    #define Y(a) a[1]
    #define M(col, row) m[(col<<1)+row]

    xv2_cpy(v, vec);
    X(o) = M(0,0)*X(v) + M(0,1)*Y(v);
    Y(o) = M(1,0)*X(v) + M(1,1)*Y(v);
    xv2_cpy(r, o);

    #undef X
    #undef Y
    #undef M
}

void
xm2_rotate(float *m, float angle)
{
    #define M(col, row) m[(col<<1)+row]
#ifdef MMX_USE_DEGREES
    float s = (float)MMX_SIN(MMX_DEG2RAD(angle));
    float c = (float)MMX_COS(MMX_DEG2RAD(angle));
#else
    float s = (float)MMX_SIN(angle);
    float c = (float)MMX_COS(angle);
#endif
    if (angle >= 0) {
        M(0,0) =  c; M(0,1) = s;
        M(1,0) = -s; M(1,1) = c;
    } else {
        M(0,0) =  c; M(0,1) = -s;
        M(1,0) =  s; M(1,1) =  c;
    }
    #undef M
}

void
xm2_scale(float *m, float x, float y)
{
    #define M(col, row) m[(col<<1)+row]
    M(0,0) = x; M(0,1) = 0;
    M(0,0) = 0; M(0,1) = y;
    #undef M
}


struct Vertex
{
    float position[2];
    float uv[2];
};

float botX, botY, botWidth, botHeight;
int AutoScreenSizing = 0;

void updateScreenLayout(GLint vbo, int screenWidth, int screenHeight)
{
    const Vertex verticesSingleScreen[] =
    {
        {-256.f/2, -192.f/2, 0.f, 0.f},
        {-256.f/2, 192.f/2, 0.f, 0.5f},
        {256.f/2, 192.f/2, 1.f, 0.5f},
        {-256.f/2, -192.f/2, 0.f, 0.f},
        {256.f/2, 192.f/2, 1.f, 0.5f},
        {256.f/2, -192.f/2, 1.f, 0.f},
    };

    Vertex vertices[12];
    memcpy(&vertices[0], verticesSingleScreen, sizeof(Vertex)*6);
    memcpy(&vertices[6], verticesSingleScreen, sizeof(Vertex)*6);

    int layout = Config::ScreenLayout == 0
        ? ((Config::ScreenRotation % 2 == 0) ? 0 : 1)
        : Config::ScreenLayout - 1;
    int rotation = Config::ScreenRotation;

    int sizing = Config::ScreenSizing == 3 ? AutoScreenSizing : Config::ScreenSizing;

    {
        float rotmat[4];
        xm2_rotate(rotmat, M_PI_2 * rotation);
        
        for (int i = 0; i < 12; i++)
            xm2_transform(vertices[i].position, rotmat, vertices[i].position);
    }

    // move screens apart
    {
        const float screenGaps[] = {0.f, 1.f, 8.f, 64.f, 90.f, 128.f};
        int idx = layout == 0 ? 1 : 0;
        float offset =
            (((layout == 0 && (rotation % 2 == 0)) || (layout == 1 && (rotation % 2 == 1)) 
                ? 192.f : 256.f)
            + screenGaps[Config::ScreenGap]) / 2.f;
        for (int i = 0; i < 6; i++)
            vertices[i].position[idx] -= offset;
        for (int i = 0; i < 6; i++)
        {
            vertices[i + 6].position[idx] += offset;
            vertices[i + 6].uv[1] += 0.5f;
        }
    }

    // scale
    {
        if (sizing == 0)
        {
            float minX = 100000.f, maxX = -100000.f;
            float minY = 100000.f, maxY = -100000.f;

            for (int i = 0; i < 12; i++)
            {
                minX = std::min(minX, vertices[i].position[0]);
                minY = std::min(minY, vertices[i].position[1]);
                maxX = std::max(maxX, vertices[i].position[0]);
                maxY = std::max(maxY, vertices[i].position[1]);
            }

            float hSize = maxX - minX;
            float vSize = maxY - minY;

            // scale evenly
            float scale = std::min(screenWidth / hSize, screenHeight / vSize);

            if (Config::IntegerScaling)
                scale = floor(scale);

            for (int i = 0; i < 12; i++)
            {
                vertices[i].position[0] *= scale;
                vertices[i].position[1] *= scale;
            }
        }
        else
        {
            int primOffset = sizing == 1 ? 0 : 6;
            int secOffset = sizing == 1 ? 6 : 0;

            float primMinX = 100000.f, primMaxX = -100000.f;
            float primMinY = 100000.f, primMaxY = -100000.f;
            float secMinX = 100000.f, secMaxX = -100000.f;
            float secMinY = 100000.f, secMaxY = -100000.f;

            for (int i = 0; i < 6; i++)
            {
                primMinX = std::min(primMinX, vertices[i + primOffset].position[0]);
                primMinY = std::min(primMinY, vertices[i + primOffset].position[1]);
                primMaxX = std::max(primMaxX, vertices[i + primOffset].position[0]);
                primMaxY = std::max(primMaxY, vertices[i + primOffset].position[1]);
            }
            for (int i = 0; i < 6; i++)
            {
                secMinX = std::min(secMinX, vertices[i + secOffset].position[0]);
                secMinY = std::min(secMinY, vertices[i + secOffset].position[1]);
                secMaxX = std::max(secMaxX, vertices[i + secOffset].position[0]);
                secMaxY = std::max(secMaxY, vertices[i + secOffset].position[1]);
            }

            float primHSize = layout == 1 ? std::max(primMaxX, -primMinX) : primMaxX - primMinX;
            float primVSize = layout == 0 ? std::max(primMaxY, -primMinY) : primMaxY - primMinY;

            float secHSize = layout == 1 ? std::max(secMaxX, -secMinX) : secMaxX - secMinX;
            float secVSize = layout == 0 ? std::max(secMaxY, -secMinY) : secMaxY - secMinY;

            float primScale = std::min(screenWidth / primHSize, screenHeight / primVSize);
            float secScale = 1.f;

            if (layout == 0)
            {
                if (screenHeight - primVSize * primScale < secVSize)
                    primScale = std::min((screenWidth - secHSize) / primHSize, (screenHeight - secVSize) / primVSize);
                else
                    secScale = std::min((screenHeight - primVSize * primScale) / secVSize, screenWidth / secHSize);
            }
            else
            {
                if (screenWidth - primHSize * primScale < secHSize)
                    primScale = std::min((screenWidth - secHSize) / primHSize, (screenHeight - secVSize) / primVSize);
                else
                    secScale = std::min((screenWidth - primHSize * primScale) / secHSize, screenHeight / secVSize);
            }

            if (Config::IntegerScaling)
                primScale = floor(primScale);
            if (Config::IntegerScaling)
                secScale = floor(secScale);

            for (int i = 0; i < 6; i++)
            {
                vertices[i + primOffset].position[0] *= primScale;
                vertices[i + primOffset].position[1] *= primScale;
            }
            for (int i = 0; i < 6; i++)
            {
                vertices[i + secOffset].position[0] *= secScale;
                vertices[i + secOffset].position[1] *= secScale;
            }
        }
    }

    // position
    {
        float minX = 100000.f, maxX = -100000.f;
        float minY = 100000.f, maxY = -100000.f;

        for (int i = 0; i < 12; i++)
        {
            minX = std::min(minX, vertices[i].position[0]);
            minY = std::min(minY, vertices[i].position[1]);
            maxX = std::max(maxX, vertices[i].position[0]);
            maxY = std::max(maxY, vertices[i].position[1]);
        }

        float width = maxX - minX;
        float height = maxY - minY;

        float botMaxX = -1000000.f, botMaxY = -1000000.f;
        float botMinX = 1000000.f, botMinY = 1000000.f;
        for (int i = 0; i < 12; i++)
        {
            vertices[i].position[0] = floor(vertices[i].position[0] - minX + screenWidth / 2 - width / 2);
            vertices[i].position[1] = floor(vertices[i].position[1] - minY + screenHeight / 2 - height / 2);

            if (i >= 6)
            {
                botMinX = std::min(vertices[i].position[0], botMinX);
                botMinY = std::min(vertices[i].position[1], botMinY);
                botMaxX = std::max(vertices[i].position[0], botMaxX);
                botMaxY = std::max(vertices[i].position[1], botMaxY);
            }
        }

        botX = botMinX;
        botY = botMinY;
        botWidth = botMaxX - botMinX;
        botHeight = botMaxY - botMinY;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 12, vertices, GL_STATIC_DRAW);
}

const char* vtxShader = R"(
    #version 330 core
    layout (location=0) in vec2 in_position;
    layout (location=1) in vec2 in_uv;
    out vec2 out_uv;
    uniform mat4 proj;
    uniform mat2 texTransform;
    void main()
    {
       gl_Position = proj * vec4(in_position, 0.0, 1.0);
       out_uv = texTransform * in_uv;
    }
)";
const char* frgShader = R"(
    #version 330 core
    out vec4 out_color;
    in vec2 out_uv;
    uniform sampler2D inTexture;
    void main()
    {
       out_color = vec4(texture(inTexture, out_uv).xyz, 1.0);
    }
)";


const u32 keyMappings[] = {
    KEY_A,
    KEY_B,
    KEY_MINUS,
    KEY_PLUS,
    KEY_DRIGHT | KEY_LSTICK_RIGHT,
    KEY_DLEFT  | KEY_LSTICK_LEFT,
    KEY_DUP    | KEY_LSTICK_UP,
    KEY_DDOWN  | KEY_LSTICK_DOWN,
    KEY_R,
    KEY_L,
    KEY_X,
    KEY_Y
};


static u64 MicWavLength;
static u64 MicBufferReadPos;
static s16* MicWavBuffer = NULL;

void loadMicSample()
{
    unsigned int channels, sampleRate;
    drwav_uint64 totalSamples;
    drwav_int16* result = drwav_open_file_and_read_pcm_frames_s16("/melonds/micsample.wav", 
        &channels, &sampleRate, &totalSamples, NULL);
    
    const u64 dstfreq = 44100;

    if (result && channels == 1 && totalSamples >= 735 && sampleRate == dstfreq)
    {
        MicWavBuffer = result;
        MicWavLength = totalSamples;
    }
}

void freeMicSample()
{
    free(MicWavBuffer);
}

void feedMicAudio(u32 state)
{
    if (!MicWavBuffer)
        return;
    if (state == 0)
    {
        NDS::MicInputFrame(NULL, 0);
        return;
    }
    if ((MicBufferReadPos + 735) > MicWavLength)
    {
        s16 tmp[735];
        u32 len1 = MicWavLength - MicBufferReadPos;
        memcpy(&tmp[0], &MicWavBuffer[MicBufferReadPos], len1*sizeof(s16));
        memcpy(&tmp[len1], &MicWavBuffer[0], (735 - len1)*sizeof(s16));

        NDS::MicInputFrame(tmp, 735);
        MicBufferReadPos = 735 - len1;
    }
    else
    {
        NDS::MicInputFrame(&MicWavBuffer[MicBufferReadPos], 735);
        MicBufferReadPos += 735;
    }
}

static bool running = true;
static bool paused = true;
static void* audMemPool = NULL;
static AudioDriver audDrv;

const int AudioSampleSize = 768 * 2 * sizeof(s16);

/*void LoadState(int slot)
{
    int prevstatus = EmuRunning;
    EmuRunning = 2;
    while (EmuStatus != 2);

    char filename[1024];

    if (slot > 0)
    {
        GetSavestateName(slot, filename, 1024);
    }
    else
    {
        char* file = uiOpenFile(MainWindow, "melonDS savestate (any)|*.ml1;*.ml2;*.ml3;*.ml4;*.ml5;*.ml6;*.ml7;*.ml8;*.mln", Config::LastROMFolder);
        if (!file)
        {
            EmuRunning = prevstatus;
            return;
        }

        strncpy(filename, file, 1023);
        filename[1023] = '\0';
        uiFreeText(file);
    }

    if (!Platform::FileExists(filename))
    {
        char msg[64];
        if (slot > 0) sprintf(msg, "State slot %d is empty", slot);
        else          sprintf(msg, "State file does not exist");
        OSD::AddMessage(0xFFA0A0, msg);

        EmuRunning = prevstatus;
        return;
    }

    u32 oldGBACartCRC = GBACart::CartCRC;

    // backup
    Savestate* backup = new Savestate("timewarp.mln", true);
    NDS::DoSavestate(backup);
    delete backup;

    bool failed = false;

    Savestate* state = new Savestate(filename, false);
    if (state->Error)
    {
        delete state;

        uiMsgBoxError(MainWindow, "Error", "Could not load savestate file.");

        // current state might be crapoed, so restore from sane backup
        state = new Savestate("timewarp.mln", false);
        failed = true;
    }

    NDS::DoSavestate(state);
    delete state;

    if (!failed)
    {
        if (Config::SavestateRelocSRAM && ROMPath[0][0]!='\0')
        {
            strncpy(PrevSRAMPath[0], SRAMPath[0], 1024);

            strncpy(SRAMPath[0], filename, 1019);
            int len = strlen(SRAMPath[0]);
            strcpy(&SRAMPath[0][len], ".sav");
            SRAMPath[0][len+4] = '\0';

            NDS::RelocateSave(SRAMPath[0], false);
        }

        bool loadedPartialGBAROM = false;

        // in case we have a GBA cart inserted, and the GBA ROM changes
        // due to having loaded a save state, we do not want to reload
        // the previous cartridge on reset, or commit writes to any
        // loaded save file. therefore, their paths are "nulled".
        if (GBACart::CartInserted && GBACart::CartCRC != oldGBACartCRC)
        {
            ROMPath[1][0] = '\0';
            SRAMPath[1][0] = '\0';
            loadedPartialGBAROM = true;
        }

        char msg[64];
        if (slot > 0) sprintf(msg, "State loaded from slot %d%s",
                        slot, loadedPartialGBAROM ? " (GBA ROM header only)" : "");
        else          sprintf(msg, "State loaded from file%s",
                        loadedPartialGBAROM ? " (GBA ROM header only)" : "");
        OSD::AddMessage(0, msg);

        SavestateLoaded = true;
        uiMenuItemEnable(MenuItem_UndoStateLoad);
    }

    EmuRunning = prevstatus;
}

int SaveState(int slot)
{
    char filename[1024];

    if (slot > 0)
    {
        GetSavestateName(slot, filename, 1024);
    }
    else
    {
        char* file = uiSaveFile(MainWindow, "melonDS savestate (*.mln)|*.mln", Config::LastROMFolder);
        if (!file)
        {
            EmuRunning = prevstatus;
            return;
        }

        strncpy(filename, file, 1023);
        filename[1023] = '\0';
        uiFreeText(file);
    }

    Savestate* state = new Savestate(filename, true);
    if (state->Error)
    {
        delete state;

        return 0;
    }
    else
    {
        NDS::DoSavestate(state);
        delete state;

        if (slot > 0)
            uiMenuItemEnable(MenuItem_LoadStateSlot[slot-1]);

        if (Config::SavestateRelocSRAM && ROMPath[0][0]!='\0')
        {
            strncpy(SRAMPath[0], filename, 1019);
            int len = strlen(SRAMPath[0]);
            strcpy(&SRAMPath[0][len], ".sav");
            SRAMPath[0][len+4] = '\0';

            NDS::RelocateSave(SRAMPath[0], true);
        }
    }

    char msg[64];
    if (slot > 0) sprintf(msg, "State saved to slot %d", slot);
    else          sprintf(msg, "State saved to file");
    OSD::AddMessage(0, msg);

    EmuRunning = prevstatus;
}

void UndoStateLoad()
{
    if (!SavestateLoaded) return;

    int prevstatus = EmuRunning;
    EmuRunning = 2;
    while (EmuStatus != 2);

    // pray that this works
    // what do we do if it doesn't???
    // but it should work.
    Savestate* backup = new Savestate("timewarp.mln", false);
    NDS::DoSavestate(backup);
    delete backup;

    if (ROMPath[0][0]!='\0')
    {
        strncpy(SRAMPath[0], PrevSRAMPath[0], 1024);
        NDS::RelocateSave(SRAMPath[0], false);
    }

    OSD::AddMessage(0, "State load undone");

    EmuRunning = prevstatus;
}*/

void setupAudio()
{
    static const AudioRendererConfig arConfig =
    {
        .output_rate     = AudioRendererOutputRate_48kHz,
        .num_voices      = 4,
        .num_effects     = 0,
        .num_sinks       = 1,
        .num_mix_objs    = 1,
        .num_mix_buffers = 2,
    };

    Result code;
    if (!R_SUCCEEDED(code = audrenInitialize(&arConfig)))
        printf("audren init failed! %d\n", code);

    if (!R_SUCCEEDED(code = audrvCreate(&audDrv, &arConfig, 2)))
        printf("audrv create failed! %d\n", code);

    const int poolSize = (AudioSampleSize * 2 + (AUDREN_MEMPOOL_ALIGNMENT-1)) & ~(AUDREN_MEMPOOL_ALIGNMENT-1);
    audMemPool = memalign(AUDREN_MEMPOOL_ALIGNMENT, poolSize);

    int mpid = audrvMemPoolAdd(&audDrv, audMemPool, poolSize);
    audrvMemPoolAttach(&audDrv, mpid);

    static const u8 sink_channels[] = { 0, 1 };
    audrvDeviceSinkAdd(&audDrv, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

    audrvUpdate(&audDrv);

    if (!R_SUCCEEDED(code = audrenStartAudioRenderer()))
        printf("audrv create failed! %d\n", code);

    if (!audrvVoiceInit(&audDrv, 0, 2, PcmFormat_Int16, 32823)) // cheating
        printf("failed to create voice\n");

    audrvVoiceSetDestinationMix(&audDrv, 0, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(&audDrv, 0, 1.0f, 0, 0);
    audrvVoiceSetMixFactor(&audDrv, 0, 1.0f, 1, 1);
    audrvVoiceStart(&audDrv, 0);
}

void audioOutput(void *args)
{
    AudioDriverWaveBuf buffers[2];
    memset(&buffers[0], 0, sizeof(AudioDriverWaveBuf) * 2);
    for (int i = 0; i < 2; i++)
    {
        buffers[i].data_pcm16 = (s16*)audMemPool;
        buffers[i].size = AudioSampleSize;
        buffers[i].start_sample_offset = i * AudioSampleSize / 2 / sizeof(s16);
        buffers[i].end_sample_offset = buffers[i].start_sample_offset + AudioSampleSize / 2 / sizeof(s16);
    }

    while (running)
    {
        while (paused && running)
        {
            svcSleepThread(17000000); // a bit more than a frame...
        }
        while (!paused && running)
        {
            AudioDriverWaveBuf* refillBuf = NULL;
            for (int i = 0; i < 2; i++)
            {
                if (buffers[i].state == AudioDriverWaveBufState_Free || buffers[i].state == AudioDriverWaveBufState_Done)
                {
                    refillBuf = &buffers[i];
                    break;
                }
            }

            if (refillBuf)
            {
                s16* data = (s16*)audMemPool + refillBuf->start_sample_offset * 2;
                
                int nSamples = 0;
                while (running && !(nSamples = SPU::ReadOutput(data, 768)))
                    svcSleepThread(1000);
                
                u32 last = ((u32*)data)[nSamples - 1];
                while (nSamples < 768)
                    ((u32*)data)[nSamples++] = last;

                armDCacheFlush(data, nSamples * 2 * sizeof(u16));
                refillBuf->end_sample_offset = refillBuf->start_sample_offset + nSamples;

                audrvVoiceAddWaveBuf(&audDrv, 0, refillBuf);
                audrvVoiceStart(&audDrv, 0);
            }

            audrvUpdate(&audDrv);
            audrenWaitFrame();
        }
    }
}

u64 sectionStartTick;
u64 sectionTicksTotal;
int entered = 0;

void EnterProfileSection()
{
    entered++;
    sectionStartTick = armGetSystemTick();
}

void CloseProfileSection()
{
    sectionTicksTotal += armGetSystemTick() - sectionStartTick;
}

ClkrstSession cpuOverclockSession;
bool usePCV;
void onAppletHook(AppletHookType hook, void *param)
{
    if (hook == AppletHookType_OnOperationMode || hook == AppletHookType_OnPerformanceMode
        || hook == AppletHookType_OnRestart || hook == AppletHookType_OnExitRequest)
    {
        applyOverclock(usePCV, &cpuOverclockSession, Config::SwitchOverclock);
    }
}

// tbh idk why I even bother with C strings
struct Filebrowser
{
    Filebrowser()
    {
        Entry entry;
        entry.isDir = true;
        entry.name = new char[3];
        strcpy(entry.name, "..");
        entries.push_back(entry);
    }

    ~Filebrowser()
    {
        delete[] entries[0].name;
    }

    void EnterDirectory(const char* path)
    {
        DIR* dir = opendir(path);
        if (dir == NULL)
        {
            path = "/";
            dir = opendir(path);
        }

        for (int i = 1; i < entries.size(); i++)
            delete[] entries[i].name;
        entries.resize(1);

        strcpy(curdir, path);

        curfile[0] = '\0';
        entryselected = NULL;
        struct dirent* cur;
        while (cur = readdir(dir))
        {
            Entry entry;
            int nameLen = strlen(cur->d_name);
            if (nameLen == 1 && cur->d_name[0] == '.')
                continue;
            if (cur->d_type == DT_REG)
            {
                if (nameLen < 4)
                    continue;
                if (cur->d_name[nameLen - 4] != '.' 
                    || cur->d_name[nameLen - 3] != 'n' 
                    || cur->d_name[nameLen - 2] != 'd' 
                    || cur->d_name[nameLen - 1] != 's')
                    continue;

                entry.name = new char[nameLen+1];
                strcpy(entry.name, cur->d_name);
                entry.isDir = false;
            }
            else if (cur->d_type == DT_DIR)
            {
                entry.name = new char[nameLen+1];
                strcpy(entry.name, cur->d_name);
                entry.isDir = true;
            }
            entries.push_back(entry);
        }

        closedir(dir);
    }

    void MoveIntoDirectory(const char* name)
    {
        int curpathlen = strlen(curdir);
        if (curpathlen > 1)
            curdir[curpathlen] = '/';
        else
            curpathlen = 0;
        strcpy(curdir + curpathlen + 1, name);
        EnterDirectory(curdir);
    }
    void MoveUpwards()
    {
        int len = strlen(curdir);
        if (len > 1)
        {
            for (int i = len - 1; i >= 0; i--)
            {
                if (curdir[i] == '/')
                {
                    if (i == 0)
                        curdir[i + 1] = '\0';
                    else
                        curdir[i] = '\0';
                    break;
                }
            }
            EnterDirectory(curdir);
        }
    }

    void Draw()
    {
        if (ImGui::BeginCombo("Browse files", curfile[0] == '\0' ? curdir : curfile))
        {
            for (int i = 0; i < entries.size(); i++)
            {
                ImGui::PushID(entries[i].name);
                if (ImGui::Selectable(entries[i].name, entryselected == entries[i].name))
                {
                    if (entries[i].isDir)
                    {
                        if (i == 0)
                            MoveUpwards();
                        else
                            MoveIntoDirectory(entries[i].name);
                    }
                    else
                    {
                        entryselected = entries[i].name;
                        strcpy(curfile, curdir);
                        int dirlen = strlen(curdir);
                        curfile[dirlen] = '/';
                        strcpy(curfile + dirlen + 1, entries[i].name);
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }

    bool HasFileSelected()
    {
        return entryselected != NULL;
    }

    struct Entry
    {
        char* name;
        bool isDir;
    };
    int curItemsCount;
    std::vector<Entry> entries;
    char curdir[256];
    char curfile[256];
    char* entryselected;
};


const int clockSpeeds[] = { 1020000000, 1224000000, 1581000000, 1785000000 };

int main(int argc, char* argv[])
{
    setenv("MESA_NO_ERROR", "1", 1);

    /*setenv("EGL_LOG_LEVEL", "debug", 1);
    setenv("MESA_VERBOSE", "all", 1);
    setenv("NOUVEAU_MESA_DEBUG", "1", 1);

    setenv("NV50_PROG_OPTIMIZE", "0", 1);
    setenv("NV50_PROG_DEBUG", "1", 1);
    setenv("NV50_PROG_CHIPSET", "0x120", 1);*/

#ifdef GDB_ENABLED
    socketInitializeDefault();
    int nxlinkSocket = nxlinkStdio();
    //GDBStub_Init();
    //GDBStub_Breakpoint();
#endif

    InitEGL(nwindowGetDefault());

    gladLoadGL();

    AppletHookCookie aptCookie;
    appletLockExit();
    appletHook(&aptCookie, onAppletHook, NULL);

    Config::Load();

    loadMicSample();

    int screenWidth, screenHeight;

    if (Config::GlobalRotation % 2 == 0)
    {
        screenWidth = 1280;
        screenHeight = 720;
    }
    else
    {
        screenWidth = 720;
        screenHeight = 1280;
    }

    usePCV = hosversionBefore(8, 0, 0);
    if (usePCV)
    {
        pcvInitialize();
    }
    else
    {
        clkrstInitialize();
        clkrstOpenSession(&cpuOverclockSession, PcvModuleId_CpuBus, 0);
    }
    applyOverclock(usePCV, &cpuOverclockSession, Config::SwitchOverclock);

    ImGui::CreateContext();
    ImGui::StyleColorsClassic();

    ImGuiStyle& style = ImGui::GetStyle();
    style.TouchExtraPadding = ImVec2(4, 4);
    style.ScaleAllSizes(2.f);

    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = 1.5f;

    ImGui_ImplOpenGL3_Init();

    GLuint screenFB;
    glGenFramebuffers(1, &screenFB);
    glBindFramebuffer(GL_FRAMEBUFFER, screenFB);
    GLuint guiTextures[2];
    glGenTextures(2, guiTextures);
    glBindTexture(GL_TEXTURE_2D, guiTextures[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2048, 2048, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, guiTextures[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, 2048, 2048, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, guiTextures[0], 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, guiTextures[1], 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    
    glEnableVertexAttribArray(0);
    glVertexAttribBinding(0, 0);
    glVertexAttribFormat(0, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribBinding(1, 0);
    glVertexAttribFormat(1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));

    GLuint vtxBuffer;
    glGenBuffers(1, &vtxBuffer);
    updateScreenLayout(vtxBuffer, screenWidth, screenHeight);

    const Vertex fullscreenQuadVertices[] = {
        {-1.f, -1.f, 0.f, 0.f}, {1.f, -1.f, 1.f, 0.f}, {1.f, 1.f, 1.f, 1.f},
        {-1.f, -1.f, 0.f, 0.f}, {1.f, 1.f, 1.f, 1.f}, {-1.f, 1.f, 0.f, 1.f}};
    GLuint fullscreenQuad;
    glGenBuffers(1, &fullscreenQuad);
    glBindBuffer(GL_ARRAY_BUFFER, fullscreenQuad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex)*6, fullscreenQuadVertices, GL_STATIC_DRAW);

    GLuint shaders[3];
    GLint projectionUniformLoc = -1, textureUniformLoc = -1, texTransformUniformLoc = -1;
    if (!OpenGL_BuildShaderProgram(vtxShader, frgShader, shaders, "GUI"))
        printf("ahhh shaders didn't compile!!!\n");
    OpenGL_LinkShaderProgram(shaders);

    projectionUniformLoc = glGetUniformLocation(shaders[2], "proj");
    textureUniformLoc = glGetUniformLocation(shaders[2], "inTexture");
    texTransformUniformLoc = glGetUniformLocation(shaders[2], "texTransform");

    GLuint screenTexture;
    glGenTextures(1, &screenTexture);
    glBindTexture(GL_TEXTURE_2D, screenTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Config::Filtering ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Config::Filtering ? GL_LINEAR : GL_NEAREST);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 256, 192 * 2);
    // swap red and blue channel
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);

    Thread audioThread;
    setupAudio();
    threadCreate(&audioThread, audioOutput, NULL, NULL, 0x8000, 0x30, 2);
    threadStart(&audioThread);

    printf("melonDS " MELONDS_VERSION "\n");
    printf(MELONDS_URL "\n");

    NDS::Init();

    Config::JIT_Enable = true;

    Config::Threaded3D = true;

    GPU3D::InitRenderer(false);

    float frametimeHistogram[60] = {0};
    float frametimeDiffHistogram[60] = {0};
    float customTimeHistogram[60] = {0};

    int guiState = 0;
    float frametimeSum = 0.f;
    float frametimeSum2 = 0.f;
    float frametimeMax = 0.f;
    float frametimeStddev = 0.f;

    const char* requiredFiles[] = {"romlist.bin", "bios9.bin", "bios7.bin", "firmware.bin"};
    int filesReady = 0;
    {
        FILE* f;
        for (int i = 0; i < sizeof(requiredFiles)/sizeof(requiredFiles[0]); i++)
        {
            if ((f = Platform::OpenLocalFile(requiredFiles[i], "rb")))
            {
                fclose(f);
                filesReady |= 1 << i;
            }
        }
    }

    bool showGui = true;
    bool navInput = true;

    FILE* perfRecord = NULL;
    int perfRecordMode = 0;

    std::vector<std::pair<u32, u32>> jitFreqResults;

    Filebrowser filebrowser;
    filebrowser.EnterDirectory(Config::LastROMFolder);
    char* romSramPath = NULL;

    bool lidClosed = false;
    u32 microphoneState = 0;

    int mainScreenPos[3];

    while (appletMainLoop())
    {
        hidScanInput();

        u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
        u32 keysUp = hidKeysUp(CONTROLLER_P1_AUTO);
        u32 keysHeld = hidKeysHeld(CONTROLLER_P1_AUTO);


        if (guiState > 0 && keysDown & KEY_ZL)
        {
            if (!showGui)
            {
                for (int i = 0; i < 12; i++)
                    NDS::ReleaseKey(i > 9 ? i + 6 : i);
                NDS::ReleaseScreen();

                NDS::MicInputFrame(NULL, 0);
                microphoneState = 0;
            }

            showGui ^= true;
            navInput = showGui;
        }

        {
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(screenWidth, screenHeight);
            io.MouseDown[0] = false;

            if (!navInput)
            {
                for (int i = 0; i < 12; i++)
                {
                    if (keysDown & keyMappings[i])
                        NDS::PressKey(i > 9 ? i + 6 : i);
                    if (keysUp & keyMappings[i])
                        NDS::ReleaseKey(i > 9 ? i + 6 : i);
                }

                if (keysDown & KEY_LSTICK)
                    microphoneState = 1;
                if (keysUp & KEY_LSTICK)
                    microphoneState = 0;

                feedMicAudio(microphoneState);
            }
            else
            {
                JoystickPosition lstick;
                hidJoystickRead(&lstick, CONTROLLER_P1_AUTO, JOYSTICK_LEFT);
            #define MAPNAV(name, key) io.NavInputs[ImGuiNavInput_##name] = keysHeld & KEY_##key ? 1.f : 0.f
                MAPNAV(Activate, A);
                MAPNAV(Cancel, B);
                MAPNAV(Input, X);
                MAPNAV(Menu, Y);
                MAPNAV(DpadLeft, DLEFT);
                MAPNAV(DpadRight, DRIGHT);
                MAPNAV(DpadUp, DUP);
                MAPNAV(DpadDown, DDOWN);
                MAPNAV(FocusNext, R);
                MAPNAV(FocusPrev, L);
                if (lstick.dy < 0)
                    io.NavInputs[ImGuiNavInput_LStickDown] = (float)lstick.dy / JOYSTICK_MIN;
                if (lstick.dy > 0)
                    io.NavInputs[ImGuiNavInput_LStickUp] = (float)lstick.dy / JOYSTICK_MAX;
                if (lstick.dx < 0)
                    io.NavInputs[ImGuiNavInput_LStickLeft] = (float)lstick.dx / JOYSTICK_MIN;
                if (lstick.dx > 0)
                    io.NavInputs[ImGuiNavInput_LStickRight] = (float)lstick.dx / JOYSTICK_MAX;
            }

            if (hidTouchCount() > 0)
            {
                io.MouseDrawCursor = false;
                touchPosition pos;
                hidTouchRead(&pos, 0);

                float rotatedTouch[2];
                switch (Config::GlobalRotation)
                {
                case 0: rotatedTouch[0] = pos.px; rotatedTouch[1] = pos.py; break;
                case 1: rotatedTouch[0] = pos.py; rotatedTouch[1] = 1280.f - pos.px; break;
                case 2: rotatedTouch[0] = 1280.f - pos.px; rotatedTouch[1] = 720.f - pos.py; break;
                case 3: rotatedTouch[0] = 720.f - pos.py; rotatedTouch[1] = pos.px; break;
                }

                if (showGui)
                {
                    io.MousePos = ImVec2(rotatedTouch[0], rotatedTouch[1]);
                    io.MouseDown[0] = true;
                }

                if (!io.WantCaptureMouse && rotatedTouch[0] >= botX && rotatedTouch[0] < (botX + botWidth) && rotatedTouch[1] >= botY && rotatedTouch[1] < (botY + botHeight))
                {
                    int x, y;
                    if (Config::ScreenRotation == 0) // 0
                    {
                        x = (rotatedTouch[0] - botX) * 256.0f / botWidth;
                        y = (rotatedTouch[1] - botY) * 256.0f / botWidth;
                    }
                    else if (Config::ScreenRotation == 1) // 90
                    {
                        x = (rotatedTouch[1] - botY) * -192.0f / botWidth;
                        y = (rotatedTouch[0] - botX) *  192.0f / botWidth;
                    }
                    else if (Config::ScreenRotation == 2) // 180
                    {
                        x =       (rotatedTouch[0] - botX) * -256.0f / botWidth;
                        y = 192 - (rotatedTouch[1] - botY) *  256.0f / botWidth;
                    }
                    else // 270
                    {
                        x =       (rotatedTouch[1] - botY) * 192.0f / botWidth;
                        y = 192 - (rotatedTouch[0] - botX) * 192.0f / botWidth;
                    }
                    NDS::PressKey(16 + 6);
                    NDS::TouchScreen(x, y);
                }
                else
                {
                    NDS::ReleaseKey(16 + 6);
                    NDS::ReleaseScreen();
                }
            }
            else
            {
                NDS::ReleaseKey(16 + 6);
                NDS::ReleaseScreen();
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, screenFB);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        glViewport(0, 0, screenWidth, screenHeight);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        paused = guiState != 1;

        if (guiState == 1)
        {
            entered = 0;
            sectionTicksTotal = 0;

            //arm9BlockFrequency.clear();
            //arm7BlockFrequency.clear();

            u64 frameStartTime = armGetSystemTick();
            NDS::RunFrame();
            u64 frameEndTime = armGetSystemTick();

            {
                mainScreenPos[2] = mainScreenPos[1];
                mainScreenPos[1] = mainScreenPos[0];
                mainScreenPos[0] = NDS::PowerControl9 >> 15;
                int guess;
                if (mainScreenPos[0] == mainScreenPos[2] &&
                    mainScreenPos[0] != mainScreenPos[1])
                {
                    // constant flickering, likely displaying 3D on both screens
                    // TODO: when both screens are used for 2D only...???
                    guess = 0;
                }
                else
                {
                    if (mainScreenPos[0] == 1)
                        guess = 1;
                    else
                        guess = 2;
                }

                if (guess != AutoScreenSizing)
                {
                    AutoScreenSizing = guess;
                    updateScreenLayout(vtxBuffer, screenWidth, screenHeight);
                }
            }

            profiler::Frame();

            for (int i = 0; i < 59; i++)
                customTimeHistogram[i] = customTimeHistogram[i + 1];
            customTimeHistogram[59] = (float)armTicksToNs(sectionTicksTotal) / 1000000.f;

            frametimeMax = 0.f;
            frametimeStddev = 0.f;
            frametimeSum = 0.f;
            frametimeSum2 = 0.f;
            for (int i = 0; i < 30; i++)
            {
                frametimeSum += frametimeHistogram[i + 1];
                frametimeHistogram[i] = frametimeHistogram[i + 1];
            }
            for (int i = 30; i < 59; i++)
            {
                frametimeSum += frametimeHistogram[i + 1];
                frametimeSum2 += frametimeHistogram[i + 1];
                frametimeHistogram[i] = frametimeHistogram[i + 1];
            }
            frametimeHistogram[59] = (float)armTicksToNs(frameEndTime - frameStartTime) / 1000000.f;
            frametimeSum += frametimeHistogram[59];
            frametimeSum /= 60.f;
            frametimeSum2 += frametimeHistogram[59];
            frametimeSum2 /= 30.f;
            for (int i = 0; i < 60; i++)
            {
                frametimeMax = std::max(frametimeHistogram[i], frametimeMax);
                float stdDevPartSqrt = frametimeHistogram[i] - frametimeSum;
                frametimeStddev += stdDevPartSqrt * stdDevPartSqrt;
            }
            frametimeStddev = sqrt(frametimeStddev / 60.f);

            if (perfRecordMode == 1)
                fwrite(&frametimeHistogram[59], 4, 1, perfRecord);
            else if (perfRecordMode == 2)
            {
                for (int i = 0; i < 59; i++)
                {
                    frametimeDiffHistogram[i] = frametimeDiffHistogram[i + 1];
                }
                float compValue;
                fread(&compValue, 4, 1, perfRecord);

                frametimeDiffHistogram[59] = compValue - frametimeHistogram[59];
            }

            /*jitFreqResults.clear();
            for (auto res : arm9BlockFrequency)
                jitFreqResults.push_back(res);
            std::sort(jitFreqResults.begin(), jitFreqResults.end(), [](std::pair<u32, u32>& a, std::pair<u32, u32>& b)
            {
                return a.second > b.second;
            });
            int totalBlockCalls = 0;
            for (int i = 0; i < jitFreqResults.size(); i++)
            {
                totalBlockCalls += jitFreqResults[i].second;
            }
            printf("top 20 blocks frametime(%f) %d out of %d\n", frametimeHistogram[59], totalBlockCalls, jitFreqResults.size());
            for (int i = 0; i < 20; i++)
            {
                printf("%x hit %dx\n", jitFreqResults[i].first, jitFreqResults[i].second);
            }*/
        }
        else if (filesReady != 0xF)
        {
            if (ImGui::Begin("Files missing!"))
            {
                ImGui::TextColored(ImVec4(1.f, 1.f, 0.f, 1.f), "Some files couldn't. Please make sure they're at the exact place:");
                for (int i = 0; i < sizeof(requiredFiles)/sizeof(requiredFiles[0]); i++)
                {
                    if (!(filesReady & (1 << i)))
                        ImGui::Text("File: /melonds/%s is missing", requiredFiles[i]);
                }
                if (ImGui::Button("Exit"))
                    break;
                
            }
            ImGui::End();
        }
        else if (guiState == 0)
        {
            if (ImGui::Begin("Select rom..."))
            {
                filebrowser.Draw();

                if (filebrowser.HasFileSelected() && ImGui::Button("Load!"))
                {
                    AutoScreenSizing = 0;
                    memset(mainScreenPos, 0, sizeof(int)*3);

                    int romNameLen = strlen(filebrowser.curfile);
                    if (romSramPath)
                        delete[] romSramPath;
                    romSramPath = new char[romNameLen + 4 + 1];
                    strcpy(romSramPath, filebrowser.curfile);
                    strcpy(romSramPath + romNameLen, ".sav");
                    NDS::LoadROM(filebrowser.curfile, romSramPath, Config::DirectBoot);

                    if (perfRecordMode == 1)
                        perfRecord = fopen("melonds_perf", "wb");
                    else if (perfRecordMode == 2)
                        perfRecord = fopen("melonds_perf", "rb");

                    guiState = 1;
                }
                
                if (ImGui::Button("Exit"))
                {
                    break;
                }

            }
            ImGui::End();

            if (ImGui::Begin("Settings"))
            {
                int globalRotation = Config::GlobalRotation;
                ImGui::Combo("Global rotation", &globalRotation, "0°\0" "90°\0" "180°\0" "270°\0");
                if (globalRotation != Config::GlobalRotation)
                {
                    Config::GlobalRotation = globalRotation;
                    if (Config::GlobalRotation % 2 == 0)
                    {
                        screenWidth = 1280;
                        screenHeight = 720;
                    }
                    else
                    {
                        screenWidth = 720;
                        screenHeight = 1280;
                    }
                    updateScreenLayout(vtxBuffer, screenWidth, screenHeight);
                }

                bool directBoot = Config::DirectBoot;
                ImGui::Checkbox("Boot games directly", &directBoot);
                Config::DirectBoot = directBoot;

                int newOverclock = Config::SwitchOverclock;
                ImGui::Combo("Overclock", &newOverclock, "1020 MHz\0" "1224 MHz\0" "1581 MHz\0" "1785 MHz\0");
                if (newOverclock != Config::SwitchOverclock)
                {
                    applyOverclock(usePCV, &cpuOverclockSession, newOverclock);
                    Config::SwitchOverclock = newOverclock;
                }
                ImGui::SliderInt("Block size", &Config::JIT_MaxBlockSize, 1, 32);
                ImGui::Checkbox("Branch optimisations", &Config::JIT_BrancheOptimisations);
                ImGui::Checkbox("Literal optimisations", &Config::JIT_LiteralOptimisations);
            }
            ImGui::End();

            if (ImGui::Begin("Profiling"))
            {
                ImGui::Combo("Mode", &perfRecordMode, "No comparision\0Write frametimes\0Compare frametimes\0");
            }
            ImGui::End();

            if (ImGui::Begin("Help"))
            {
                ImGui::BulletText("Put roms into /roms/ds");
                ImGui::BulletText("Use the Dpad to navigate the GUI");
                ImGui::BulletText("Press A to select");
                ImGui::BulletText("Press B to cancel");
                ImGui::BulletText("Use Y and...");
                ImGui::BulletText("L/R to switch between windows");
                ImGui::BulletText("the left analogstick to move windows");
                ImGui::BulletText("the Dpad to resize windows");
            }
            ImGui::End();

            if (!MicWavBuffer)
            {
                if (ImGui::Begin("Couldn't load mic sample"))
                {
                    ImGui::BulletText("You can proceed but microphone input won't be available\n");
                    ImGui::BulletText("Make sure to put the sample into /melonds/micsample.wav");
                    ImGui::BulletText("The file has to be saved as 44100Hz mono 16-bit signed pcm and be atleast 1/60s long");
                }
                ImGui::End();
            }
        }

        if (guiState > 0)
        {
            OpenGL_UseShaderProgram(shaders);
            glBindVertexArray(vao);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 8);

            glBindVertexBuffer(0, vtxBuffer, 0, sizeof(Vertex));

            glBindTexture(GL_TEXTURE_2D, screenTexture);
            for (int i = 0; i < 2; i++)
            {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 192 * i, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, GPU::Framebuffer[GPU::FrontBuffer][i]);
            }
            glUniform1i(textureUniformLoc, 0);
            float proj[16];
            xm4_orthographic(proj, 0.f, screenWidth, screenHeight, 0.f, -1.f, 1.f);
            float texTransform[4] = {1.f, 0.f, 0.f, 1.f,};
            glUniformMatrix4fv(projectionUniformLoc, 1, GL_FALSE, proj);
            glUniformMatrix2fv(texTransformUniformLoc, 1, GL_FALSE, texTransform);
            glDrawArrays(GL_TRIANGLES, 0, 12);

            glBindVertexArray(0);

            if (showGui)
            {
                ImGui::Begin("Navigation");
                if (navInput)
                    navInput = navInput && !ImGui::Button("Give key input back to game");
                else
                    ImGui::Text("Hide and unhide the GUI to regain key input");
                ImGui::End();

                if (ImGui::Begin("Perf", NULL, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("frametime avg1: %fms avg2: %fms std dev: +/%fms max: %fms %d", frametimeSum, frametimeSum2, frametimeStddev, frametimeMax, entered);
                    ImGui::PlotHistogram("Frametime history", frametimeHistogram, 60, 0, NULL, 0.f, 25.f, ImVec2(0, 50.f));
                    
                    ImGui::PlotHistogram("Custom counter", customTimeHistogram, 60, 0, NULL, 0.f, 25.f, ImVec2(0, 50.f));

                    if (perfRecordMode == 2)
                    {
                        ImGui::PlotHistogram("Frametime diff", frametimeDiffHistogram, 60, 0, NULL, -25.f, 25.f, ImVec2(0, 50.f));
                    }

                    profiler::Render();

                }
                ImGui::End();

                if (ImGui::Begin("Display settings"))
                {
                    bool displayDirty = false;

                    int newSizing = Config::ScreenSizing;
                    ImGui::Combo("Screen Sizing", &newSizing, "Even\0Emphasise top\0Emphasise bottom\0Auto\0");
                    displayDirty |= newSizing != Config::ScreenSizing;

                    int newRotation = Config::ScreenRotation;
                    const char* rotations[] = {"0°", "90°", "180°", "270°"};
                    ImGui::Combo("Screen Rotation", &newRotation, rotations, 4);
                    displayDirty |= newRotation != Config::ScreenRotation;

                    int newGap = Config::ScreenGap;
                    const char* screenGaps[] = {"0px", "1px", "8px", "64px", "90px", "128px"};
                    ImGui::Combo("Screen Gap", &newGap, screenGaps, 6);
                    displayDirty |= newGap != Config::ScreenGap;

                    int newLayout = Config::ScreenLayout;
                    ImGui::Combo("Screen Layout", &newLayout, "Natural\0Vertical\0Horizontal\0");
                    displayDirty |= newLayout != Config::ScreenLayout;

                    bool newIntegerScale = Config::IntegerScaling;
                    ImGui::Checkbox("Integer Scaling", &newIntegerScale);
                    displayDirty |= newIntegerScale != Config::IntegerScaling;

                    if (displayDirty)
                    {
                        Config::ScreenSizing = newSizing;
                        Config::ScreenRotation = newRotation;
                        Config::ScreenGap = newGap;
                        Config::ScreenLayout = newLayout;
                        Config::IntegerScaling = newIntegerScale;

                        updateScreenLayout(vtxBuffer, screenWidth, screenHeight);
                    }

                    bool newFiltering = Config::Filtering;
                    ImGui::Checkbox("Filtering", &newFiltering);
                    if (newFiltering != Config::Filtering)
                    {
                        glBindTexture(GL_TEXTURE_2D, screenTexture);
                        GLenum glFilter = newFiltering ? GL_LINEAR : GL_NEAREST;
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glFilter);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glFilter);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        Config::Filtering = newFiltering;
                    }
                }
                ImGui::End();

                if (ImGui::Begin("Emusettings", NULL, ImGuiWindowFlags_AlwaysAutoResize))
                {
                    if (ImGui::Checkbox("Lid closed", &lidClosed))
                        NDS::SetLidClosed(lidClosed);
                    if (ImGui::Button("Reset"))
                    {
                        NDS::LoadROM(filebrowser.curfile, romSramPath, true);

                        if (perfRecord)
                        {
                            fseek(perfRecord, SEEK_SET, 0);
                        }
                    }
                    if (ImGui::Button("Stop"))
                    {
                        if (perfRecord)
                        {
                            fclose(perfRecord);
                            perfRecord = NULL;
                        }
                        guiState = 0;
                        navInput = true;
                    }
                    if (guiState == 1 && ImGui::Button("Pause"))
                        guiState = 2;
                    if (guiState == 2 && ImGui::Button("Unpause"))
                        guiState = 1;

                }
                ImGui::End();
            }
        }

        ImGui::Render();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glViewport(0, 0, 1280, 720);
        glClear(GL_COLOR_BUFFER_BIT);

        {
            glBindVertexArray(vao);

            OpenGL_UseShaderProgram(shaders);

            glBindTexture(GL_TEXTURE_2D, guiTextures[0]);
            glUniform1i(textureUniformLoc, 0);
            float proj[16];
            float texTransform[4] = {screenWidth/2048.f, 0.f, 0.f, screenHeight/2048.f};
            xm4_orthographic(proj, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f);
            float rot[16];
            xm4_rotatef(rot, M_PI_2 * Config::GlobalRotation, 0.f, 0.f, 1.f);
            xm4_mul(proj, proj, rot);
            glUniformMatrix4fv(projectionUniformLoc, 1, GL_FALSE, proj);
            glUniformMatrix2fv(texTransformUniformLoc, 1, GL_FALSE, texTransform);
            glBindVertexBuffer(0, fullscreenQuad, 0, sizeof(Vertex));
            glDrawArrays(GL_TRIANGLES, 0, 6);

            glBindVertexArray(0);
        }

        eglSwapBuffers(eglDisplay, eglSurface);
    }

    if (perfRecord)
    {
        fclose(perfRecord);
        perfRecord = NULL;
    }

    NDS::DeInit();

    strcpy(Config::LastROMFolder, filebrowser.curdir);

    Config::Save();

    if (romSramPath)
        delete[] romSramPath;

    running = false;
    threadWaitForExit(&audioThread);
    threadClose(&audioThread);

    audrvClose(&audDrv);
    audrenExit();

    free(audMemPool);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();

    DeInitEGL();

    applyOverclock(usePCV, &cpuOverclockSession, 0);
    if (usePCV)
    {
        pcvExit();
    }
    else
    {
        clkrstCloseSession(&cpuOverclockSession);
        clkrstExit();
    }

    freeMicSample();

    appletUnhook(&aptCookie);
    appletUnlockExit();

#ifdef GDB_ENABLED
    close(nxlinkSocket);
    socketExit();
    //GDBStub_Shutdown();
#endif

    return 0;
}