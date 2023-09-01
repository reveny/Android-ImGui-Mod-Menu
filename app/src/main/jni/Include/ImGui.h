//
// Created by Reveny on 2022/12/25.
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

void menuStyle();
void (*menuAddress)();

using swapbuffers_orig = EGLBoolean (*)(EGLDisplay dpy, EGLSurface surf);
EGLBoolean swapbuffers_hook(EGLDisplay dpy, EGLSurface surf);
swapbuffers_orig o_swapbuffers = nullptr;

bool isInitialized = false;
int glWidth = 0;
int glHeight = 0;

//Taken from https://github.com/fedes1to/Zygisk-ImGui-Menu/blob/main/module/src/main/cpp/hook.cpp
#define HOOKINPUT(ret, func, ...) \
    ret (*orig##func)(__VA_ARGS__); \
    ret my##func(__VA_ARGS__)

HOOKINPUT(void, Input, void *thiz, void *ex_ab, void *ex_ac) {
    origInput(thiz, ex_ab, ex_ac);
    ImGui_ImplAndroid_HandleInputEvent((AInputEvent *)thiz);
    return;
}

//This menu_addr is used to allow for multiple game support in the future
void *initModMenu(void *menu_addr) {
    menuAddress = (void (*)())menu_addr;
    do {
        sleep(1);
    } while (!isLibraryLoaded(OBFUSCATE("libEGL.so")));

    auto swapBuffers = ((uintptr_t) DobbySymbolResolver(OBFUSCATE("libEGL.so"), OBFUSCATE("eglSwapBuffers")));
    KittyMemory::ProtectAddr((void *)swapBuffers, sizeof(swapBuffers), PROT_READ | PROT_WRITE | PROT_EXEC);
    DobbyHook((void *) swapBuffers, (void *) swapbuffers_hook, (void **) &o_swapbuffers);

    //Taken from https://github.com/fedes1to/Zygisk-ImGui-Menu/blob/main/module/src/main/cpp/hook.cpp
    void *sym_input = DobbySymbolResolver(OBFUSCATE("/system/lib/libinput.so"), OBFUSCATE("_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE"));
    if (sym_input != nullptr) {
        DobbyHook((void *) sym_input, (void *) myInput, (void **) &origInput);
    }

    LOGI(OBFUSCATE("ImGUI Hooks initialized"));
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
    LOGI("setup done.");
}
void internalDrawMenu(int width, int height) {
    if(!isInitialized) return;

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
