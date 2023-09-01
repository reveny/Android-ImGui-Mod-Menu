#ifndef UTILS
#define UTILS

#include <jni.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
#include <inttypes.h>
#include <sys/uio.h>

#include <libgen.h>

#if defined(__arm__)
#define process_vm_readv_syscall 376
#define process_vm_writev_syscall 377
#elif defined(__aarch64__)
#define process_vm_readv_syscall 270
#define process_vm_writev_syscall 271
#elif defined(__i386__)
#define process_vm_readv_syscall 347
#define process_vm_writev_syscall 348
#else
#define process_vm_readv_syscall 310
#define process_vm_writev_syscall 311
#endif

ssize_t process_v(pid_t __pid, const struct iovec *__local_iov, unsigned long __local_iov_count, const struct iovec *__remote_iov, unsigned long __remote_iov_count, unsigned long __flags, bool iswrite) {
    return syscall((iswrite ? process_vm_writev_syscall : process_vm_readv_syscall), __pid, __local_iov, __local_iov_count, __remote_iov, __remote_iov_count, __flags);
}

bool pvm(void *address, void *buffer, size_t size, bool write = false) {
    struct iovec local[1];
    struct iovec remote[1];

    local[0].iov_base = buffer;
    local[0].iov_len = size;
    remote[0].iov_base = address;
    remote[0].iov_len = size;

    ssize_t bytes = process_v(getpid(), local, 1, remote, 1, 0, write);
    return bytes == size;
}

bool ReadAddr(void *addr, void *buffer, size_t length) {
    return pvm(addr, buffer, length, false);
}

typedef unsigned long DWORD;
static uintptr_t libBase;

bool isGameLibLoaded = false;

DWORD findLibrary(const char *library) {
    char filename[0xFF] = {0},
            buffer[1024] = {0};
    FILE *fp = NULL;
    DWORD address = 0;

    pid_t pid = getpid();
    sprintf(filename, "/proc/%d/maps", pid);

    fp = fopen(filename, "rt");
    if (fp == NULL) {
        perror("fopen");
        goto done;
    }

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (strstr(buffer, library)) {
            address = (DWORD) strtoul(buffer, NULL, 16);
            goto done;
        }
    }

    done:
    if (fp) {
        fclose(fp);
    }

    return address;
}

uintptr_t getBaseAddress(const char *name) {
    uintptr_t base = 0;
    char line[512];

    FILE *f = fopen("/proc/self/maps", "r");

    if (!f) {
        return 0;
    }

    while (fgets(line, sizeof line, f)) {
        uintptr_t tmpBase;
        char tmpName[256];
        if (sscanf(line, "%" PRIXPTR "-%*" PRIXPTR " %*s %*s %*s %*s %s", &tmpBase, tmpName) > 0) {
            if (!strcmp(basename(tmpName), name)) {
                base = tmpBase;
                break;
            }
        }
    }

    fclose(f);
    return base;
}

uintptr_t getAbsoluteAddress(const char *libraryName, uintptr_t relativeAddr) {
    libBase = findLibrary(libraryName);
    if (libBase == 0)
        return 0;
    return (libBase + relativeAddr);
}

bool isLibraryLoaded(const char *libraryName) {
    //isGameLibLoaded = true;
    char line[512] = {0};

    FILE *fp = fopen("/proc/self/maps", "rt");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp)) {
            std::string a = line;
            if (strstr(line, libraryName)) {
                isGameLibLoaded = true;
                return true;
            }
        }
        fclose(fp);
    }
    return false;
}

uintptr_t string2Offset(const char *c) {
    int base = 16;
    // See if this function catches all possibilities.
    // If it doesn't, the function would have to be amended
    // whenever you add a combination of architecture and
    // compiler that is not yet addressed.
    static_assert(sizeof(uintptr_t) == sizeof(unsigned long)
                  || sizeof(uintptr_t) == sizeof(unsigned long long),
                  "Please add string to handle conversion for this architecture.");

    // Now choose the correct function ...
    if (sizeof(uintptr_t) == sizeof(unsigned long)) {
        return strtoul(c, nullptr, base);
    }

    // All other options exhausted, sizeof(uintptr_t) == sizeof(unsigned long long))
    return strtoull(c, nullptr, base);
}

namespace Toast {
    inline const int LENGTH_LONG = 1;
    inline const int LENGTH_SHORT = 0;
}

struct lib_info {
    void* start_address;
    void* end_address;
    intptr_t size;
    std::string name;
};

lib_info find_library(const char *module_name){
    lib_info library_info{};
    char line[512], mod_name[64];

    FILE *fp = fopen("/proc/self/maps", "rt");
    if (fp != NULL)
    {
        while (fgets(line, sizeof(line), fp))
        {
            if (strstr(line, module_name))
            {
                sscanf(line, "%llx-%llx %*s %*ld %*s %*d %s",
                       (long long unsigned *)&library_info.start_address,
                       (long long unsigned *)&library_info.end_address, mod_name);

                library_info.size = (uintptr_t)library_info.end_address - (uintptr_t)library_info.start_address;

                if (library_info.name.empty())
                    library_info.name = std::string(mod_name);

                break;
            }
        }
        fclose(fp);
    }
    return library_info;
}

uintptr_t find_pattern(uint8_t* start, const size_t length, const char* pattern) {
    const char* pat = pattern;
    uint8_t* first_match = 0;
    for (auto current_byte = start; current_byte < (start + length); ++current_byte) {
        if (*pat == '?' || *current_byte == strtoul(pat, NULL, 16)) {
            if (!first_match)
                first_match = current_byte;
            if (!pat[2])
                return (uintptr_t)first_match;
            pat += *(uint16_t*)pat == 16191 || *pat != '?' ? 3 : 2;
        }
        else if (first_match) {
            current_byte = first_match;
            pat = pattern;
            first_match = 0;
        }
    } return 0;
}

uintptr_t find_pattern_in_module(const char* lib_name, const char* pattern) {
    lib_info lib_info = find_library(lib_name);
    return find_pattern((uint8_t*)lib_info.start_address, lib_info.size, pattern);
}

uintptr_t find_pattern_in_module_opcode(const char* lib_name, const char* pattern) {
    lib_info lib_info = find_library(lib_name);
    uintptr_t addr = find_pattern((uint8_t*)lib_info.start_address, lib_info.size, pattern);
    void* realAddr = nullptr;
    ReadAddr((void*)addr, realAddr, sizeof(void*));
    return (uintptr_t)realAddr;
}

#endif