// Defina VK_USE_PLATFORM_XLIB_KHR antes de incluir os cabeçalhos do Vulkan
#define VK_USE_PLATFORM_XLIB_KHR

#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <dlfcn.h>
#include <unistd.h>
#include <cstring>
#include <optional>
#include <set>
#include <algorithm>
#include <vulkan/vulkan.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

bool overlayEnabled = false;
bool insertWasPressed = false;
Window targetWindow = 0;
Display* xDisplay = nullptr;

typedef void (*HydraInitFunc)();
typedef void (*HydraUpdateFunc)();
typedef void (*HydraCleanupFunc)();
void* HydraHandle = nullptr;
HydraInitFunc HydraInit = nullptr;
HydraUpdateFunc HydraUpdate = nullptr;
HydraCleanupFunc HydraCleanup = nullptr;

std::atomic<bool> running(true);
std::thread inputThread;
std::atomic<bool> hydraActive(false);

void initializeX() {
    xDisplay = XOpenDisplay(NULL);
    if (!xDisplay) {
        std::cerr << "[Wtf] Erro ao abrir o display X11." << std::endl;
    }
}

void cleanupX() {
    if (xDisplay) {
        XCloseDisplay(xDisplay);
        xDisplay = nullptr;
    }
}

void initializeHydra() {
    HydraHandle = dlopen("./Hydra.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!HydraHandle) {
        std::cerr << "[Wtf] Erro ao carregar Hydra.so: " << dlerror() << std::endl;
        return;
    }

    HydraInit = (HydraInitFunc)dlsym(HydraHandle, "HYDRA_Init");
    HydraUpdate = (HydraUpdateFunc)dlsym(HydraHandle, "HYDRA_Update");
    HydraCleanup = (HydraCleanupFunc)dlsym(HydraHandle, "HYDRA_Cleanup");

    if (!HydraInit || !HydraUpdate) {
        std::cerr << "[Wtf] Funções do Hydra não encontradas!" << std::endl;
        dlclose(HydraHandle);
        HydraHandle = nullptr;
        return;
    }

    HydraInit();
    hydraActive.store(true);
}

void cleanupHydra() {
    hydraActive.store(false);
    if (HydraCleanup) HydraCleanup();
    if (HydraHandle) {
        dlclose(HydraHandle);
        HydraHandle = nullptr;
    }
}

void toggleOverlay() {
    overlayEnabled = !overlayEnabled;
    std::cout << "[Wtf] Overlay " << (overlayEnabled ? "ativado" : "desativado") << std::endl;

    if (overlayEnabled) {
        Window focused;
        int revert;
        XGetInputFocus(xDisplay, &focused, &revert);
        targetWindow = (focused == None || focused == PointerRoot) ?
                       DefaultRootWindow(xDisplay) : focused;
        std::cout << "[Wtf] Janela alvo: 0x" << std::hex << targetWindow << std::dec << std::endl;
        initializeHydra();
    } else {
        cleanupHydra();
    }
}

void checkInput() {
    if (!xDisplay) return;
    char keys[32];
    XQueryKeymap(xDisplay, keys);
    KeyCode insertKey = XKeysymToKeycode(xDisplay, XK_Insert);
    bool insertPressedNow = keys[insertKey >> 3] & (1 << (insertKey & 7));

    if (insertPressedNow && !insertWasPressed) toggleOverlay();
    insertWasPressed = insertPressedNow;
}

void updateOverlay() {
    checkInput();
    if (overlayEnabled && HydraUpdate && hydraActive.load()) HydraUpdate();
}

extern "C" void Wtf_Update() {
    updateOverlay();
}

__attribute__((constructor))
void onLoad() {
    initializeX();
    inputThread = std::thread([]() {
        while (running) {
            updateOverlay();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
}

__attribute__((destructor))
void onUnload() {
    running = false;
    if (inputThread.joinable()) inputThread.join();
    cleanupX();
    cleanupHydra();
}
