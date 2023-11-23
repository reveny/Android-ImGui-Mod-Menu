//
// Created by reveny on 21/08/2023.
//

#include <stdio.h>
#include <string>
#include <jni.h>
#include <dlfcn.h>
#include <unistd.h>

#include "../Include/Obfuscate.h"
#include "../Include/Logger.h"
#include "../Include/RemapTools.h"

std::string GetNativeLibraryDirectory() {
    char buffer[512];
    FILE *fp = fopen("/proc/self/maps", "re");
    if (fp != nullptr) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            if (strstr(buffer, "libLoader.so")) {
                RemapTools::ProcMapInfo info{};
                char perms[10];
                char path[255];
                char dev[25];

                sscanf(buffer, "%lx-%lx %s %ld %s %ld %s", &info.start, &info.end, perms, &info.offset, dev, &info.inode, path);
                info.path = path;

                //Remove libLoader.so to get path
                std::string pathStr = std::string(info.path);
                if (!pathStr.empty()) {
                    pathStr.resize(pathStr.size() - 12); //libLoader.so = 12 chars
                }

                return pathStr;
            }
        }
    }
    return "";
}

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("libLoader.so loaded in %d", getpid());
    std::string native = GetNativeLibraryDirectory();
    if (native.empty()) {
        LOGE("Error getting native library directory");
        exit(1);
    }

    LOGI("Found native library directory: %s", native.c_str());
    std::string path = native + "libModMenu.so";

    // Open the library containing the actual code
    void *open = dlopen(path.c_str(), RTLD_NOW);
    if (open == nullptr) {
        LOGE("Error opening libTest.so %s", dlerror());
        return JNI_ERR;
    }

    // Call JNI in library
    void *jni_load = dlsym(open, "loadJNI");
    if (jni_load == nullptr) {
        LOGE("Failed to find symbol loadJNI()");
        return JNI_ERR;
    }

    auto loadJNI = (jint (*)(JavaVM *vm)) jni_load;
    jint jni = loadJNI(vm); // Call function in main library

    RemapTools::RemapLibrary("libModMenu.so");

    return jni;
}