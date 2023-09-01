//
// Created by reveny on 10/07/2023.
// Inspired by riru and zygisk
//

#include <cstdint>
#include <link.h>
#include <sys/mman.h>
#include <vector>
#include <string>

namespace RemapTools {
    struct ProcMapInfo {
        uintptr_t start;
        uintptr_t end;
        uintptr_t offset;
        uint8_t perms;
        ino_t inode;
        char* dev;
        char* path;
    };

    std::vector<ProcMapInfo> ListModulesWithName(std::string name) {
        std::vector<ProcMapInfo> returnVal;

        char buffer[512];
        FILE *fp = fopen("/proc/self/maps", "re");
        if (fp != nullptr) {
            while (fgets(buffer, sizeof(buffer), fp)) {
                if (strstr(buffer, name.c_str())) {
                    ProcMapInfo info{};
                    char perms[10];
                    char path[255];
                    char dev[25];

                    sscanf(buffer, "%lx-%lx %s %ld %s %ld %s", &info.start, &info.end, perms, &info.offset, dev, &info.inode, path);

                    //Process Perms
                    if (strchr(perms, 'r')) info.perms |= PROT_READ;
                    if (strchr(perms, 'w')) info.perms |= PROT_WRITE;
                    if (strchr(perms, 'x')) info.perms |= PROT_EXEC;
                    if (strchr(perms, 'r')) info.perms |= PROT_READ;

                    //Set all other information
                    info.dev = dev;
                    info.path = path;

                    LOGI("Line: %s", buffer);
                    returnVal.push_back(info);
                }
            }
        }
        return returnVal;
    }

    void RemapLibrary(std::string name) {
        std::vector<ProcMapInfo> maps = ListModulesWithName(name);

        for (ProcMapInfo info : maps) {
            void *address = (void *)info.start;
            size_t size = info.end - info.start;
            void *map = mmap(0, size, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

            if ((info.perms & PROT_READ) == 0) {
                LOGI("Removing protection: %s", info.path);
                mprotect(address, size, PROT_READ);
            }

            if (map == nullptr) {
                LOGE("Failed to Allocate Memory: %s", strerror(errno));
            }
            LOGI("Allocated at address %p with size of %zu", map, size);

            //Copy to new location
            std::memmove(map, address, size);
            mremap(map, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, info.start);

            //Reapply protection
            mprotect((void *)info.start, size, info.perms);
        }
    }
}
