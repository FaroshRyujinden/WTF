// Defina VK_USE_PLATFORM_XLIB_KHR antes de incluir os cabeçalhos do Vulkan
#define VK_USE_PLATFORM_XLIB_KHR

// Inclua os cabeçalhos necessários
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
#include <set>       // Para std::set
#include <algorithm> // Para std::max e std::min

// Bibliotecas Vulkan e X11
#include <vulkan/vulkan.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

// Variáveis globais
bool overlayEnabled = false;
bool insertWasPressed = false;
Window targetWindow = 0;

// Ponteiro para o display X11
Display* xDisplay = nullptr;

// Ponteiros para as funções do Hydra.so
typedef void (*HydraInitFunc)();
typedef void (*HydraUpdateFunc)();
typedef void (*HydraCleanupFunc)();
void* HydraHandle = nullptr;
HydraInitFunc HydraInit = nullptr;
HydraUpdateFunc HydraUpdate = nullptr;
HydraCleanupFunc HydraCleanup = nullptr;

// Gerenciamento de thread
std::atomic<bool> running(true);
std::thread inputThread;

// Variável atômica para controlar o estado do Hydra
std::atomic<bool> hydraActive(false);

// Funções
void initializeX();
void cleanupX();
void initializeHydra();
void cleanupHydra();
void toggleOverlay();
void checkInput();
void updateOverlay();

// Implementações

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
    HydraHandle = dlopen("./Hydra.so", RTLD_LAZY | RTLD_DEEPBIND);
    if (!HydraHandle) {
        std::cerr << "[Wtf] Erro ao carregar Hydra.so: " << dlerror() << std::endl;
        return;
    }

    HydraInit = (HydraInitFunc)dlsym(HydraHandle, "HYDRA_Init");
    if (!HydraInit) {
        std::cerr << "[Wtf] Erro ao localizar HYDRA_Init: " << dlerror() << std::endl;
        dlclose(HydraHandle);
        HydraHandle = nullptr;
        return;
    }

    HydraUpdate = (HydraUpdateFunc)dlsym(HydraHandle, "HYDRA_Update");
    if (!HydraUpdate) {
        std::cerr << "[Wtf] Erro ao localizar HYDRA_Update: " << dlerror() << std::endl;
        dlclose(HydraHandle);
        HydraHandle = nullptr;
        return;
    }

    HydraCleanup = (HydraCleanupFunc)dlsym(HydraHandle, "HYDRA_Cleanup");
    if (!HydraCleanup) {
        std::cerr << "[Wtf] Erro ao localizar HYDRA_Cleanup: " << dlerror() << std::endl;
        // A função de limpeza pode ser opcional, dependendo da implementação
    }

    // Inicializa o subsistema Hydra (Vulkan)
    HydraInit();

    // Define hydraActive como true
    hydraActive.store(true);
}

void cleanupHydra() {
    // Define hydraActive como false para evitar chamadas subsequentes a HydraUpdate
    hydraActive.store(false);

    if (HydraCleanup) {
        HydraCleanup();
    }

    if (HydraHandle) {
        dlclose(HydraHandle);
        HydraHandle = nullptr;
        HydraInit = nullptr;
        HydraUpdate = nullptr;
        HydraCleanup = nullptr;
    }
}

void toggleOverlay() {
    overlayEnabled = !overlayEnabled;
    std::cout << "[Wtf] Overlay " << (overlayEnabled ? "ativado" : "desativado") << std::endl;

    if (overlayEnabled) {
        // Obter a janela alvo
        if (xDisplay) {
            Window focused;
            int revert;
            XGetInputFocus(xDisplay, &focused, &revert);
            targetWindow = (focused == None || focused == PointerRoot) ?
                           DefaultRootWindow(xDisplay) : focused;

            std::cout << "[Wtf] Janela alvo: 0x" << std::hex << targetWindow << std::dec << std::endl;
        }

        // Inicializar o Hydra
        initializeHydra();
    } else {
        // Limpar recursos do Hydra
        cleanupHydra();
    }
}

void checkInput() {
    if (!xDisplay) return;

    char keys[32];
    XQueryKeymap(xDisplay, keys);
    KeyCode insertKey = XKeysymToKeycode(xDisplay, XK_Insert);
    bool insertPressedNow = keys[insertKey >> 3] & (1 << (insertKey & 7));

    if (insertPressedNow && !insertWasPressed) {
        toggleOverlay();
    }
    insertWasPressed = insertPressedNow;
}

void updateOverlay() {
    checkInput();
    if (overlayEnabled && HydraUpdate && hydraActive.load()) {
        HydraUpdate();
    }
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

