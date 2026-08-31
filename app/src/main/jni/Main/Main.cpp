//
// Created by reveny on 21/08/2023.
//

#include "../Include/KittyMemory/MemoryPatch.h"
#include "../Include/ImGui.h"
#include "../Include/RemapTools.h"

#include "../Include/Drawing.h"
#include "../Include/Unity.h"

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>

// JNI Support
JavaVM *jvm = nullptr;
JNIEnv *env = nullptr;

static const std::chrono::steady_clock::time_point kStartTime = std::chrono::steady_clock::now();

// Toggle state for the Basic panel's movement modifications. Intended to be
// read each frame by a movement-hook module (not implemented in this file —
// see the hook stub in thread() below) that patches player velocity,
// collision, and respawn behavior.
namespace ModState {
    bool modFly = false;         // alters player vertical velocity to simulate creative flight
    bool antiLava = false;       // detects lava tiles and prevents damage events
    bool antiRespawn = false;    // intercepts respawn triggers and restores position
    bool seeLockedDoor = false;  // reads door ownership labels without interacting
    bool noclipGhost = false;    // removes collision boundaries while keeping visual rendering active
    bool visualInvisV2 = false;  // adjusts player render opacity to zero while keeping input handling
}

// Toggle state for the Visual panel. Intended to be read each frame by a
// render/overlay hook module (not implemented in this file) that overrides
// lighting, entity render flags, and UI element visibility.
namespace VisualState {
    bool nightVision = false;    // overrides ambient light values to full brightness
    bool canSeeGhost = false;    // enables visibility of normally hidden entities by toggling their render flags
    bool seeInsideChest = false; // forces chest inventory data to render regardless of lock state
    bool seeFruit = false;       // reveals harvestable node labels and icons on the minimap
    bool fastTake = false;       // bypasses pickup delay timers by setting interval values to zero
    bool noName = false;         // disables player name label rendering
}

// Target coordinates for the FindPath panel. Intended to be read by a
// navigation-hook module (not implemented in this file): it reads
// targetX/targetY, and on teleportRequested it writes the player's position
// and clears the flag once the override has been applied.
namespace FindPathState {
    int inputX = 0;
    int inputY = 0;
    int targetX = 0;
    int targetY = 0;
    bool teleportRequested = false;
}

// Speed multipliers and movement toggles for the Fast panel. Intended to be
// read each frame by the same movement-hook module as ModState (not
// implemented in this file) to scale velocity and invert input direction.
namespace FastState {
    float moveSpeedMultiplier = 1.0f;
    float fallSpeedMultiplier = 1.0f;
    bool noWalk = false;      // disables movement animation
    bool moonwalk = false;    // reverses movement direction input
}

// Toggle state for the ESP panel. Intended to be read each frame by an
// ESP render module (not implemented in this file) that draws the
// corresponding overlay for each nearby entity.
namespace EspState {
    bool showName = false;       // renders nametags over entities
    bool showHealthBar = false;  // draws health indicators above players
    bool showItemGlow = false;   // applies highlight borders to valuable items on ground
    bool showDistance = false;   // shows meter values under entity names
    bool showBox = false;        // draws colored bounding boxes around players and NPCs
    bool showLine = false;       // draws connection lines from player to nearby entities
}

// State for the Extract panel. Intended to be read by a collection-routine
// module (not implemented in this file, and out of scope for this menu
// framework) that would consume extractRequested and append picked-up item
// names to extractedItems for display; extractedItems is otherwise only
// ever cleared here via the "Clear List" button.
namespace ExtractState {
    bool extractRequested = false;
    std::vector<std::string> extractedItems;
}

// Persists the menu window's position/size across sessions.
namespace MenuState {
    ImVec2 position = ImVec2(60.0f, 60.0f);
    ImVec2 size = ImVec2(520.0f, 380.0f);
    bool loaded = false;

    // Resolves the current app's private files directory via JNI so the
    // config file survives regardless of which app the menu is injected into.
    std::string GetConfigPath() {
        std::string path;

        if (jvm != nullptr) {
            JNIEnv *localEnv = nullptr;
            bool attached = false;

            if (jvm->GetEnv((void **)&localEnv, JNI_VERSION_1_6) != JNI_OK) {
                attached = jvm->AttachCurrentThread(&localEnv, nullptr) == JNI_OK;
            }

            if (localEnv != nullptr) {
                jclass activityThreadClass = localEnv->FindClass(OBFUSCATE("android/app/ActivityThread"));
                if (activityThreadClass != nullptr) {
                    jmethodID currentApplication = localEnv->GetStaticMethodID(activityThreadClass, OBFUSCATE("currentApplication"), OBFUSCATE("()Landroid/app/Application;"));
                    jobject app = currentApplication ? localEnv->CallStaticObjectMethod(activityThreadClass, currentApplication) : nullptr;

                    if (app != nullptr) {
                        jclass contextClass = localEnv->GetObjectClass(app);
                        jmethodID getFilesDir = localEnv->GetMethodID(contextClass, OBFUSCATE("getFilesDir"), OBFUSCATE("()Ljava/io/File;"));
                        jobject filesDir = getFilesDir ? localEnv->CallObjectMethod(app, getFilesDir) : nullptr;

                        if (filesDir != nullptr) {
                            jclass fileClass = localEnv->GetObjectClass(filesDir);
                            jmethodID getAbsolutePath = localEnv->GetMethodID(fileClass, OBFUSCATE("getAbsolutePath"), OBFUSCATE("()Ljava/lang/String;"));
                            auto pathStr = (jstring)(getAbsolutePath ? localEnv->CallObjectMethod(filesDir, getAbsolutePath) : nullptr);

                            if (pathStr != nullptr) {
                                const char *chars = localEnv->GetStringUTFChars(pathStr, nullptr);
                                // OBFUSCATE(...) implicitly converts to both char* and
                                // std::string, so std::string + OBFUSCATE(...) is ambiguous
                                // unless routed through a plain const char* first.
                                const char *suffix = OBFUSCATE("/.modmenu_state");
                                path = std::string(chars) + suffix;
                                localEnv->ReleaseStringUTFChars(pathStr, chars);
                                localEnv->DeleteLocalRef(pathStr);
                            }
                            localEnv->DeleteLocalRef(filesDir);
                        }
                        localEnv->DeleteLocalRef(app);
                    }
                    localEnv->DeleteLocalRef(activityThreadClass);
                }

                if (localEnv->ExceptionCheck()) {
                    localEnv->ExceptionClear();
                    path.clear();
                }

                if (attached) {
                    jvm->DetachCurrentThread();
                }
            }
        }

        if (path.empty()) {
            // Same ambiguity as above: assign via a const char* local, not
            // directly from OBFUSCATE(...), to force a single conversion.
            const char *fallback = OBFUSCATE("/data/local/tmp/.modmenu_state");
            path = fallback;
        }

        return path;
    }

    void Load() {
        if (loaded) return;
        loaded = true;

        FILE *f = fopen(GetConfigPath().c_str(), "r");
        if (f != nullptr) {
            fscanf(f, "%f %f %f %f", &position.x, &position.y, &size.x, &size.y);
            fclose(f);
        }
    }

    void Save() {
        FILE *f = fopen(GetConfigPath().c_str(), "w");
        if (f != nullptr) {
            fprintf(f, "%f %f %f %f", position.x, position.y, size.x, size.y);
            fclose(f);
        }
    }
}

void menuStyle() {
    ImGui::StyleColorsDark();

    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    style.WindowRounding = 6.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.92f, 0.93f, 1.00f);
    colors[ImGuiCol_TextDisabled]          = ImVec4(0.50f, 0.50f, 0.53f, 1.00f);
    colors[ImGuiCol_WindowBg]              = ImVec4(0.08f, 0.08f, 0.09f, 0.96f);
    colors[ImGuiCol_ChildBg]               = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
    colors[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.08f, 0.09f, 0.98f);
    colors[ImGuiCol_Border]                = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_FrameBg]               = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_FrameBgActive]         = ImVec4(0.25f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_TitleBg]               = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
    colors[ImGuiCol_TitleBgActive]         = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
    colors[ImGuiCol_Button]                = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(0.30f, 0.55f, 0.90f, 0.60f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.30f, 0.55f, 0.90f, 0.90f);
    colors[ImGuiCol_Header]                = ImVec4(0.30f, 0.55f, 0.90f, 0.55f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(0.30f, 0.55f, 0.90f, 0.75f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(0.30f, 0.55f, 0.90f, 0.90f);
    colors[ImGuiCol_CheckMark]             = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]            = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.40f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.06f, 0.06f, 0.07f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.20f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.35f, 0.35f, 0.40f, 1.00f);
    colors[ImGuiCol_Separator]             = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_ResizeGrip]            = ImVec4(0.30f, 0.55f, 0.90f, 0.40f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.30f, 0.55f, 0.90f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.30f, 0.55f, 0.90f, 0.95f);
}

// Category content panels

static void DrawBasicPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Basic"));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox(OBFUSCATE("ModFly"), &ModState::modFly);
    ImGui::Checkbox(OBFUSCATE("Anti Lava"), &ModState::antiLava);
    ImGui::Checkbox(OBFUSCATE("Anti Respawn"), &ModState::antiRespawn);
    ImGui::Checkbox(OBFUSCATE("See Locked Door"), &ModState::seeLockedDoor);
    ImGui::Checkbox(OBFUSCATE("Noclip and Ghost"), &ModState::noclipGhost);
    ImGui::Checkbox(OBFUSCATE("Visual Invis V2"), &ModState::visualInvisV2);
}

static void DrawVisualPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Visual"));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox(OBFUSCATE("Night Vision"), &VisualState::nightVision);
    ImGui::Checkbox(OBFUSCATE("Can See Ghost"), &VisualState::canSeeGhost);
    ImGui::Checkbox(OBFUSCATE("See Inside Chest"), &VisualState::seeInsideChest);
    ImGui::Checkbox(OBFUSCATE("See Fruit"), &VisualState::seeFruit);
    ImGui::Checkbox(OBFUSCATE("Fast Take"), &VisualState::fastTake);
    ImGui::Checkbox(OBFUSCATE("No Name"), &VisualState::noName);
}

static void DrawFindPathPanel() {
    ImGui::TextUnformatted(OBFUSCATE("FindPath"));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::InputInt(OBFUSCATE("X"), &FindPathState::inputX);
    ImGui::InputInt(OBFUSCATE("Y"), &FindPathState::inputY);

    if (ImGui::Button(OBFUSCATE("Set Target"))) {
        FindPathState::targetX = FindPathState::inputX;
        FindPathState::targetY = FindPathState::inputY;
    }

    ImGui::SameLine();

    if (ImGui::Button(OBFUSCATE("Teleport"))) {
        FindPathState::teleportRequested = true;
    }
}

static void DrawFastPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Fast"));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::SliderFloat(OBFUSCATE("Move Speed"), &FastState::moveSpeedMultiplier, 1.0f, 10.0f);
    ImGui::SliderFloat(OBFUSCATE("Fall Speed"), &FastState::fallSpeedMultiplier, 0.1f, 5.0f);
    ImGui::Checkbox(OBFUSCATE("No Walk"), &FastState::noWalk);
    ImGui::Checkbox(OBFUSCATE("Moonwalk"), &FastState::moonwalk);
}

static void DrawEspPanel() {
    ImGui::TextUnformatted(OBFUSCATE("ESP"));
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox(OBFUSCATE("Player Name"), &EspState::showName);
    ImGui::Checkbox(OBFUSCATE("Health Bar"), &EspState::showHealthBar);
    ImGui::Checkbox(OBFUSCATE("Item Glow"), &EspState::showItemGlow);
    ImGui::Checkbox(OBFUSCATE("Distance"), &EspState::showDistance);
    ImGui::Checkbox(OBFUSCATE("Box"), &EspState::showBox);
    ImGui::Checkbox(OBFUSCATE("Line"), &EspState::showLine);
}

static void DrawExtractPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Extract"));
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button(OBFUSCATE("Extract All"))) {
        ExtractState::extractRequested = true;
    }

    ImGui::SameLine();

    if (ImGui::Button(OBFUSCATE("Clear List"))) {
        ExtractState::extractedItems.clear();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(OBFUSCATE("Extracted Items"));
    ImGui::BeginChild("##extracted_items", ImVec2(0.0f, 0.0f), true);
    for (const std::string &item : ExtractState::extractedItems) {
        ImGui::TextUnformatted(item.c_str());
    }
    ImGui::EndChild();
}

static double GetUptimeSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - kStartTime).count();
}

static long GetMemoryUsageMB() {
    FILE *f = fopen("/proc/self/status", "r");
    if (f == nullptr) return 0;

    long memoryKB = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &memoryKB);
            break;
        }
    }
    fclose(f);

    return memoryKB / 1024;
}

static bool IsBasicCategoryActive() {
    return ModState::modFly || ModState::antiLava || ModState::antiRespawn ||
           ModState::seeLockedDoor || ModState::noclipGhost || ModState::visualInvisV2;
}

static bool IsVisualCategoryActive() {
    return VisualState::nightVision || VisualState::canSeeGhost || VisualState::seeInsideChest ||
           VisualState::seeFruit || VisualState::fastTake || VisualState::noName;
}

static bool IsFastCategoryActive() {
    return FastState::noWalk || FastState::moonwalk ||
           FastState::moveSpeedMultiplier != 1.0f || FastState::fallSpeedMultiplier != 1.0f;
}

static bool IsEspCategoryActive() {
    return EspState::showName || EspState::showHealthBar || EspState::showItemGlow ||
           EspState::showDistance || EspState::showBox || EspState::showLine;
}

// Ternary expressions can't pick between two OBFUSCATE(...) calls directly:
// each call's return type bakes in the string's length, so two different
// literals produce two different (unrelated) types. Routing both through a
// same-typed `const char*` parameter here sidesteps that.
static const char *SelectLabel(bool condition, const char *whenTrue, const char *whenFalse) {
    return condition ? whenTrue : whenFalse;
}

static void DrawInfoPanel() {
    ImGui::TextUnformatted(OBFUSCATE("Info"));
    ImGui::Separator();
    ImGui::Spacing();

    ImGuiIO &io = ImGui::GetIO();
    float fps = (io.DeltaTime > 0.0f) ? (1.0f / io.DeltaTime) : 0.0f;

    ImGui::Text(OBFUSCATE("FPS: %.1f"), fps);
    ImGui::Text(OBFUSCATE("Memory: %ld MB"), GetMemoryUsageMB());
    ImGui::Text(OBFUSCATE("Status: %s"), SelectLabel(isInitialized, OBFUSCATE("Running"), OBFUSCATE("Initializing")));
    ImGui::Text(OBFUSCATE("Uptime: %.0f s"), GetUptimeSeconds());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted(OBFUSCATE("Active Categories"));
    ImGui::BulletText(OBFUSCATE("Basic: %s"), SelectLabel(IsBasicCategoryActive(), OBFUSCATE("Active"), OBFUSCATE("Idle")));
    ImGui::BulletText(OBFUSCATE("Visual: %s"), SelectLabel(IsVisualCategoryActive(), OBFUSCATE("Active"), OBFUSCATE("Idle")));
    ImGui::BulletText(OBFUSCATE("Fast: %s"), SelectLabel(IsFastCategoryActive(), OBFUSCATE("Active"), OBFUSCATE("Idle")));
    ImGui::BulletText(OBFUSCATE("ESP: %s"), SelectLabel(IsEspCategoryActive(), OBFUSCATE("Active"), OBFUSCATE("Idle")));
}

struct Category {
    const char *name;
    void (*draw)();
};

static Category categories[] = {
    { "Basic",    DrawBasicPanel },
    { "Visual",   DrawVisualPanel },
    { "FindPath", DrawFindPathPanel },
    { "Fast",     DrawFastPanel },
    { "ESP",      DrawEspPanel },
    { "Extract",  DrawExtractPanel },
    { "Info",     DrawInfoPanel },
};

static int activeCategory = 0;

void DrawMenu() {
    MenuState::Load();

    ImGui::SetNextWindowPos(MenuState::position, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(MenuState::size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 280.0f), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin(OBFUSCATE("Mod Menu"), nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    float footerHeight = ImGui::GetFrameHeightWithSpacing();

    ImGui::BeginChild("##sidebar", ImVec2(130.0f, -footerHeight), true);
    for (int i = 0; i < IM_ARRAYSIZE(categories); i++) {
        bool selected = (activeCategory == i);

        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        }

        if (ImGui::Button(categories[i].name, ImVec2(-FLT_MIN, 0.0f))) {
            activeCategory = i;
        }

        if (selected) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##content", ImVec2(0.0f, -footerHeight), true);
    categories[activeCategory].draw();
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextUnformatted(SelectLabel(isInitialized, OBFUSCATE("Ready"), OBFUSCATE("Initializing")));

    // Persist position/size once the drag or resize has settled.
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    if (pos.x != MenuState::position.x || pos.y != MenuState::position.y ||
        size.x != MenuState::size.x || size.y != MenuState::size.y) {
        MenuState::position = pos;
        MenuState::size = size;

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            MenuState::Save();
        }
    }

    ImGui::End();
}

void *thread(void *) {
    LOGI(OBFUSCATE("Main Thread Loaded: %d"), gettid());
    initModMenu((void *)DrawMenu);

    //Hooks, Patches and Pointers here
    //Example:
    /*
     * DobbyHook(getAbsoluteAddress("libIl2cpp.so", 0x0), FunctionExample, old_FunctionExample);
     * SetAimRotation = (void (*)(void *, Quaternion)) getAbsoluteAddress("libIl2cpp.so", 0x0);
     */

    LOGI("Main thread done");
    pthread_exit(0);
}

// Call anything from JNI_OnLoad here
extern "C" {
    __attribute__((visibility ("default")))
    jint loadJNI(JavaVM *vm) {
        jvm = vm;
        vm->AttachCurrentThread(&env, nullptr);
        LOGI("loadJNI(): Initialized");

        return JNI_VERSION_1_6;
    }
}

__attribute__((constructor))
void init() {
    LOGI("Loaded Mod Menu");

    pthread_t t;
    pthread_create(&t, nullptr, thread, nullptr);

    //Don't leave any traces, remap the loader lib as well
    RemapTools::RemapLibrary("libLoader.so");
}
