//
// Created by Reveny on 2022/12/25.
// Updated 27.01.2026 (Android 15+)
// ORIGIN ANDROID 15 - NOT WORK (tested)
// OTHERS OS - WORK (tested)
//

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <asm-generic/mman.h>
#include <sys/mman.h>

#include "ImGui/imgui.h"
#include "Roboto-Regular.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "ImGui/backends/imgui_impl_android.h"
#include "ImGui/backends/android_native_app_glue.h"

#include "Utils.h"
#include "Dobby/dobby.h"
#include "Obfuscate.h"
#include "Logger.h"

//YOUR XDL PATH
#include <../Include/xdl/include/xdl.h>

void menuStyle();
void (*menuAddress)();

using swapbuffers_orig = EGLBoolean (*)(EGLDisplay dpy, EGLSurface surf);
EGLBoolean swapbuffers_hook(EGLDisplay dpy, EGLSurface surf);
swapbuffers_orig o_swapbuffers = nullptr;

bool isInitialized = false;
int glWidth = 0;
int glHeight = 0;

//input Hooks from https://github.com/NepMods/LibInput-Hook-Research-For-Imgui-Touch
//InitializeMotionEvent (Android < 15)
static void (*origInput)(void *thiz, void *ex_ab, void *ex_ac);
void myInput(void *thiz, void *ex_ab, void *ex_ac) {
    origInput(thiz, ex_ab, ex_ac);
    ImGui_ImplAndroid_HandleInputEvent((AInputEvent *)thiz);
}

// 64-bit sign
static int32_t (*origConsume64)(void* consumer,
                                 void* factory,
                                 bool isRaw,
                                 long sequenceId,
                                 uint32_t* outPolicyFlags,
                                 void** outEventPtr);

int32_t myConsume64(void* consumer,
                    void* factory,
                    bool isRaw,
                    long sequenceId,
                    uint32_t* outPolicyFlags,
                    void** outEventPtr) {
    int32_t result = origConsume64(consumer,
                                    factory,
                                    isRaw,
                                    sequenceId,
                                    outPolicyFlags,
                                    outEventPtr);

    if (result == 0 && outEventPtr && *outEventPtr) {
        AInputEvent* event = reinterpret_cast<AInputEvent*>(*outEventPtr);
        ImGui_ImplAndroid_HandleInputEvent(event);
    }

    return result;
}

// 32-bit sign
static int32_t (*origConsume32)(void* consumer,
                                 void* factory,
                                 bool isRaw,
                                 int64_t sequenceId,  
                                 uint32_t* outPolicyFlags,
                                 void** outEventPtr);

int32_t myConsume32(void* consumer,
                    void* factory,
                    bool isRaw,
                    int64_t sequenceId,
                    uint32_t* outPolicyFlags,
                    void** outEventPtr) {
    int32_t result = origConsume32(consumer,
                                    factory,
                                    isRaw,
                                    sequenceId,
                                    outPolicyFlags,
                                    outEventPtr);

    if (result == 0 && outEventPtr && *outEventPtr) {
        AInputEvent* event = reinterpret_cast<AInputEvent*>(*outEventPtr);
        ImGui_ImplAndroid_HandleInputEvent(event);
    }

    return result;
}

//Android < 15
#define SYMBOL_INIT_MOTION_EVENT \
    "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE"

//Android 15+ (64-bit)
#define SYMBOL_CONSUME_64 \
    "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE"

//Android 15+ (32bit)
#define SYMBOL_CONSUME_32 \
    "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEbxPjPPNS_10InputEventE"


#if defined(__aarch64__) || defined(__x86_64__)
    #define IS_64BIT 1
    #define LIBINPUT_PATH "/system/lib64/libinput.so"
#else
    #define IS_64BIT 0
    #define LIBINPUT_PATH "/system/lib/libinput.so"
#endif


void setupInputHooks() {
    void* sym_input = nullptr;
    
    sym_input = DobbySymbolResolver(
        OBFUSCATE(LIBINPUT_PATH), 
        OBFUSCATE(SYMBOL_INIT_MOTION_EVENT)
    );
    
    if (sym_input != nullptr) {
        LOGI("Found initializeMotionEvent (Android < 15)");
        DobbyHook(sym_input, (void*)myInput, (void**)&origInput);
        return;
    }
    
    LOGI("initializeMotionEvent not found, trying consume() for Android 15+");
    
    #if IS_64BIT
        sym_input = DobbySymbolResolver(
            OBFUSCATE(LIBINPUT_PATH),
            OBFUSCATE(SYMBOL_CONSUME_64)
        );
        
        if (sym_input != nullptr) {
            LOGI("Found consume() 64-bit at %p", sym_input);
            DobbyHook(sym_input, (void*)myConsume64, (void**)&origConsume64);
            return;
        }
    #else
        sym_input = DobbySymbolResolver(
            OBFUSCATE(LIBINPUT_PATH),
            OBFUSCATE(SYMBOL_CONSUME_32)
        );
        
        if (sym_input != nullptr) {
            LOGI("Found consume() 32-bit at %p", sym_input);
            DobbyHook(sym_input, (void*)myConsume32, (void**)&origConsume32);
            return;
        }
    #endif
    LOGE("Failed to find any input hook symbol");
}

void *initModMenu(void *menu_addr) {
    menuAddress = (void (*)())menu_addr;
    
    do {
        sleep(1);
    } while (!isLibraryLoaded(OBFUSCATE("libEGL.so")));

    // Хук eglSwapBuffers
    auto swapBuffers = ((uintptr_t)DobbySymbolResolver(
        OBFUSCATE("libEGL.so"), 
        OBFUSCATE("eglSwapBuffers")
    ));
    
    KittyMemory::ProtectAddr(
        (void*)swapBuffers, 
        sizeof(swapBuffers), 
        PROT_READ | PROT_WRITE | PROT_EXEC
    );
    
    DobbyHook(
        (void*)swapBuffers, 
        (void*)swapbuffers_hook, 
        (void**)&o_swapbuffers
    );

  

    LOGI(OBFUSCATE("ImGUI Hooks initialized!!! "));
    return nullptr;
}

void setupMenu() {
    if (isInitialized) return;

    auto ctx = ImGui::CreateContext();
    if (!ctx) {
        LOGI(OBFUSCATE("Failed to create context"));
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)glWidth, (float)glHeight);
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.IniFilename = nullptr;

    // Setup Platform/Renderer backends
    ImGui_ImplAndroid_Init();
    ImGui_ImplOpenGL3_Init("#version 300 es");

    int systemScale = (1.0 / glWidth) * glWidth;
    ImFontConfig font_cfg;
    font_cfg.SizePixels = systemScale * 22.0f;
    io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, systemScale * 30.0, 40.0f);

    ImGui::GetStyle().ScaleAllSizes(2);

    isInitialized = true;
    LOGI("Setup done.");
}

void internalDrawMenu(int width, int height) {
    if (!isInitialized) return;

    ImGuiIO &io = ImGui::GetIO();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(width, height);
    ImGui::NewFrame();

    menuAddress();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

EGLBoolean swapbuffers_hook(EGLDisplay dpy, EGLSurface surf) {
    EGLint w, h;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    glWidth = w;
    glHeight = h;

    setupMenu();
    internalDrawMenu(w, h);

    return o_swapbuffers(dpy, surf);
}
