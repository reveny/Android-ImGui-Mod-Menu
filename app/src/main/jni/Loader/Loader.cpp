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

__attribute__((constructor))
void init() {
    LOGI("libLoader.so loaded in %d", getpid());
    std::string native = GetNativeLibraryDirectory();
    if (native.empty()) {
        LOGE("Error getting native library directory");
        exit(1);
    }

    LOGI("Found native library directory: %s", native.c_str());
    std::string path = native + "libModMenu.so";

    //Open the library containing the actual code
    void *open = dlopen(path.c_str(), RTLD_NOW);
    if (open == nullptr) {
        LOGE("Error opening libTest.so %s", dlerror());
    }

    RemapTools::RemapLibrary("libModMenu.so");
}