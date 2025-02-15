// Defina VK_USE_PLATFORM_XLIB_KHR antes de incluir os cabeçalhos do Vulkan
#define VK_USE_PLATFORM_XLIB_KHR

#include <vulkan/vulkan.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <optional>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <filesystem> // Para manipulação de caminhos de forma moderna
#include <glm/glm.hpp>
#include <glm/vec2.hpp>
#include <array>

// --- Novo: Criar vertex buffer ---
// Definição da estrutura de vértice e dados de exemplo
struct Vertex {
    glm::vec2 pos;
    glm::vec2 texCoord;
};

const std::vector<Vertex> vertices = {
    {{-1.0f, -1.0f}, {0.0f, 0.0f}},
    {{ 1.0f, -1.0f}, {1.0f, 0.0f}},
    {{ 1.0f,  1.0f}, {1.0f, 1.0f}},
    {{-1.0f,  1.0f}, {0.0f, 1.0f}}
};

// ================================
// VARIÁVEIS GLOBAIS RELACIONADAS AO VULKAN
// ================================
VkInstance         g_instance            = VK_NULL_HANDLE;
VkSurfaceKHR       g_surface             = VK_NULL_HANDLE;
VkPhysicalDevice   g_physicalDevice      = VK_NULL_HANDLE;
VkDevice           g_device              = VK_NULL_HANDLE;
VkQueue            g_graphicsQueue       = VK_NULL_HANDLE;
VkQueue            g_presentQueue        = VK_NULL_HANDLE;
uint32_t           g_graphicsQueueFamily = 0;
uint32_t           g_presentQueueFamily  = 0;

VkSwapchainKHR                   g_swapchain         = VK_NULL_HANDLE;
std::vector<VkImage>             g_swapchainImages;
std::vector<VkImageView>         g_swapchainImageViews;
VkFormat                         g_swapchainImageFormat;
VkExtent2D                       g_swapchainExtent;
VkRenderPass                     g_renderPass        = VK_NULL_HANDLE;
VkPipelineLayout                 g_pipelineLayout    = VK_NULL_HANDLE;
VkPipeline                       g_graphicsPipeline  = VK_NULL_HANDLE;
VkCommandPool                    g_commandPool       = VK_NULL_HANDLE;
std::vector<VkFramebuffer>       g_framebuffers;
VkSemaphore                      g_imageAvailableSemaphore = VK_NULL_HANDLE;
VkSemaphore                      g_renderFinishedSemaphore = VK_NULL_HANDLE;
uint32_t                         vertexCount         = 3; // Exemplo: triângulo

// Variáveis para o frame interpolado
VkImage         g_interpolatedImage = VK_NULL_HANDLE;
VkDeviceMemory  g_interpolatedImageMemory = VK_NULL_HANDLE;
VkImageView     g_interpolatedImageView = VK_NULL_HANDLE;
VkSampler       g_textureSampler = VK_NULL_HANDLE;
VkDescriptorSetLayout g_descriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorPool g_descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet  g_descriptorSet = VK_NULL_HANDLE;

// Vertex buffer
VkBuffer         g_vertexBuffer = VK_NULL_HANDLE;
VkDeviceMemory   g_vertexBufferMemory = VK_NULL_HANDLE;

// Variável atômica para controlar o overlay
std::atomic<bool> overlayActive(false);

// Ponteiros X11 – devem ser definidos externamente (no seu ambiente, estes serão configurados)
extern Display* xDisplay;
extern Window   targetWindow;

// ================================
// ESTRUTURAS AUXILIARES
// ================================
struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Estrutura para armazenar dados dos frames capturados
struct FrameData {
    VkImage   image;
    uint8_t*  pixels;
    uint32_t  width, height;
};

FrameData lastFrame    = { VK_NULL_HANDLE, nullptr, 0, 0 };
FrameData currentFrame = { VK_NULL_HANDLE, nullptr, 0, 0 };

// ================================
// PROTÓTIPOS DE FUNÇÕES AUXILIARES
// ================================
void createInstance();
void createSurface();
void pickPhysicalDevice();
void findQueueFamilies(VkPhysicalDevice device);
void createLogicalDevice();
SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device);
VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
VkPresentModeKHR   chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
VkExtent2D         chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
void createSwapchain();
void createImageViews();
void createRenderPass();
void createGraphicsPipeline();
void createCommandPool();
void createFramebuffers();
void createSyncObjects();
VkShaderModule createShaderModule(const std::vector<char>& code);
std::vector<char> readFile(const std::string& filename);
void recreateSwapchain();
void cleanupSwapchain();
VkCommandBuffer beginSingleTimeCommands();
void endSingleTimeCommands(VkCommandBuffer commandBuffer);

// Funções auxiliares para buffer e imagem
void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  VkBuffer &buffer, VkDeviceMemory &bufferMemory);
uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout,
                           VkImageLayout newLayout, VkCommandBuffer commandBuffer);
void CopyImageToBuffer(VkImage image, VkBuffer buffer, VkExtent2D extent, VkCommandBuffer commandBuffer);


// ================================
// FUNÇÃO: createVertexBuffer()
// ================================
// Cria o vertex buffer a partir dos vértices definidos.
void createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 g_vertexBuffer, g_vertexBufferMemory);

    void* data;
    vkMapMemory(g_device, g_vertexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(g_device, g_vertexBufferMemory);
}

// ================================
// FUNÇÕES DE HOOK (INTERCEPTAÇÃO)
// ================================
static PFN_vkQueuePresentKHR real_vkQueuePresentKHR = nullptr;
static PFN_vkAcquireNextImageKHR real_vkAcquireNextImageKHR = nullptr;
static PFN_vkQueueSubmit       real_vkQueueSubmit       = nullptr;

// ================================
// INTEGRAÇÃO COM Omg.so
// ================================
typedef void (*GenerateOmgFrameFunc)(float, uint8_t*, uint8_t*, uint32_t, uint32_t, uint8_t*);
GenerateOmgFrameFunc pGenerateOmgFrame = nullptr;
void* omgHandle = nullptr;

void LoadOmgModule(const std::filesystem::path& exePath) {
    std::filesystem::path libPath = exePath.parent_path() / "FG" / "Omg.so";
    std::cout << "[Hydra] Tentando carregar Omg.so de: " << libPath << std::endl;
    if (!std::filesystem::exists(libPath)) {
        std::cerr << "[Hydra] Omg.so não encontrado no diretório: " << libPath << std::endl;
        return;
    }
    omgHandle = dlopen(libPath.c_str(), RTLD_LAZY);
    if (!omgHandle) {
        std::cerr << "[Hydra] Falha ao carregar Omg.so: " << dlerror() << std::endl;
    } else {
        std::cout << "[Hydra] Omg.so carregado com sucesso!" << std::endl;
    }
}

// Declaração da função HYDRA_Init
extern "C" void HYDRA_Init(const std::filesystem::path& exePath);

// ================================
// FUNÇÃO MAIN
// ================================
int main(int argc, char* argv[]) {
    std::filesystem::path exePath = std::filesystem::absolute(argv[0]);
    std::cout << "[Hydra] Caminho absoluto do executável: " << exePath << std::endl;
    
    // Cria o vertex buffer
    createVertexBuffer();

    // Chama a função de inicialização passando o caminho do executável
    HYDRA_Init(exePath);

    return 0;
}

// ================================
// IMPLEMENTAÇÃO DA FUNÇÃO HYDRA_Init
// ================================
extern "C" void HYDRA_Init(const std::filesystem::path& exePath) {
    std::cout << "[Hydra] Inicializando Vulkan..." << std::endl;
    overlayActive.store(true);
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createCommandPool();
    createFramebuffers();
    createSyncObjects();

    // Passa o exePath para LoadOmgModule
    LoadOmgModule(exePath);    
    
    // --- Criar imagem para o quadro interpolado ---
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageInfo.extent = { g_swapchainExtent.width, g_swapchainExtent.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(g_device, &imageInfo, nullptr, &g_interpolatedImage) != VK_SUCCESS) {
        throw std::runtime_error("Falha ao criar imagem interpolada!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_device, g_interpolatedImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(g_device, &allocInfo, nullptr, &g_interpolatedImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Falha ao alocar memória para imagem interpolada!");
    }
    vkBindImageMemory(g_device, g_interpolatedImage, g_interpolatedImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = g_interpolatedImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(g_device, &viewInfo, nullptr, &g_interpolatedImageView) != VK_SUCCESS) {
        throw std::runtime_error("Falha ao criar image view para imagem interpolada!");
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    if (vkCreateSampler(g_device, &samplerInfo, nullptr, &g_textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("Falha ao criar sampler!");
    }

    // --- Criar Descriptor Set Layout e Pool ---
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorCount = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(g_device, &layoutInfo, nullptr, &g_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Falha ao criar descriptor set layout!");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(g_device, &poolInfo, nullptr, &g_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Falha ao criar descriptor pool!");
    }

    VkDescriptorSetAllocateInfo allocDescInfo{};
    allocDescInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocDescInfo.descriptorPool = g_descriptorPool;
    allocDescInfo.descriptorSetCount = 1;
    allocDescInfo.pSetLayouts = &g_descriptorSetLayout;

    if (vkAllocateDescriptorSets(g_device, &allocDescInfo, &g_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Falha ao alocar descriptor sets!");
    }

    VkDescriptorImageInfo descriptorImageInfo{};
    descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorImageInfo.imageView   = g_interpolatedImageView;
    descriptorImageInfo.sampler     = g_textureSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = g_descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descriptorImageInfo;

    vkUpdateDescriptorSets(g_device, 1, &descriptorWrite, 0, nullptr);
}

// ================================
// FUNÇÃO HYDRA_Update (chamada a cada frame)
// ================================
extern "C" void HYDRA_Update() {
    if (!overlayActive.load()) return;
    if (g_device == VK_NULL_HANDLE || g_commandPool == VK_NULL_HANDLE ||
        g_graphicsPipeline == VK_NULL_HANDLE || g_swapchain == VK_NULL_HANDLE) {
        std::cerr << "[Hydra] Recursos não inicializados corretamente." << std::endl;
        return;
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX,
                                             g_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS) {
        std::cerr << "[Hydra] Falha ao adquirir imagem da swapchain." << std::endl;
        return;
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = g_renderPass;
    renderPassInfo.framebuffer = g_framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = g_swapchainExtent;
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, g_graphicsPipeline);
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    // --- Novo: Vincular o descriptor set ---
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
        g_pipelineLayout, 0, 1, &g_descriptorSet, 0, nullptr);

    // Desenhar um quad (6 vértices)
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Falha ao gravar command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = { g_imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    VkSemaphore signalSemaphores[] = { g_renderFinishedSemaphore };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(g_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        std::cerr << "[Hydra] Falha ao submeter o command buffer." << std::endl;
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = { g_swapchain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(g_presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || !overlayActive.load()) {
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        std::cerr << "[Hydra] Falha ao apresentar a imagem da swapchain." << std::endl;
        return;
    }

    vkQueueWaitIdle(g_presentQueue);
}

// ================================
// FUNÇÃO HYDRA_Cleanup (libera recursos)
// ================================
extern "C" void HYDRA_Cleanup() {
    std::cout << "[Hydra] Iniciando cleanup()" << std::endl;
    overlayActive.store(false);
    if (g_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(g_device);
    }
    cleanupSwapchain();
    if (g_graphicsPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(g_device, g_graphicsPipeline, nullptr);
        g_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (g_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(g_device, g_pipelineLayout, nullptr);
        g_pipelineLayout = VK_NULL_HANDLE;
    }
    if (g_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(g_device, g_renderPass, nullptr);
        g_renderPass = VK_NULL_HANDLE;
    }
    if (g_imageAvailableSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(g_device, g_imageAvailableSemaphore, nullptr);
        g_imageAvailableSemaphore = VK_NULL_HANDLE;
    }
    if (g_renderFinishedSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(g_device, g_renderFinishedSemaphore, nullptr);
        g_renderFinishedSemaphore = VK_NULL_HANDLE;
    }
    if (g_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(g_device, g_commandPool, nullptr);
        g_commandPool = VK_NULL_HANDLE;
    }
    if (g_device != VK_NULL_HANDLE) {
        vkDestroyDevice(g_device, nullptr);
        g_device = VK_NULL_HANDLE;
    }
    if (g_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
        g_surface = VK_NULL_HANDLE;
    }
    if (g_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(g_instance, nullptr);
        g_instance = VK_NULL_HANDLE;
    }
    if (omgHandle) {
        dlclose(omgHandle);
        omgHandle = nullptr;
    }
    
    if (g_interpolatedImage != VK_NULL_HANDLE) {
        vkDestroyImage(g_device, g_interpolatedImage, nullptr);
        vkFreeMemory(g_device, g_interpolatedImageMemory, nullptr);
        vkDestroyImageView(g_device, g_interpolatedImageView, nullptr);
    }
    
    if (g_textureSampler != VK_NULL_HANDLE) {
        vkDestroySampler(g_device, g_textureSampler, nullptr);
    }

    if (g_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(g_device, g_descriptorSetLayout, nullptr);
    }

    if (g_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(g_device, g_descriptorPool, nullptr);
    }
    if (lastFrame.pixels) { free(lastFrame.pixels); lastFrame.pixels = nullptr; }
    if (currentFrame.pixels) { free(currentFrame.pixels); currentFrame.pixels = nullptr; }
}

// ================================
// FUNÇÃO DE CAPTURA DE FRAME (hook)
// ================================
void CaptureFrame(VkCommandBuffer commandBuffer, VkImage image) {
    std::cout << "[Debug] CaptureFrame chamado!" << std::endl;
    
    VkExtent2D extent = g_swapchainExtent;
    VkDeviceSize imageSize = extent.width * extent.height * 4;  // Assume RGBA (4 bytes por pixel)

    // Captura do frame original
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    VkCommandBuffer tempCmdBuffer = beginSingleTimeCommands();

    // Transição para leitura: de PRESENT_SRC para TRANSFER_SRC
    TransitionImageLayout(image, VK_FORMAT_B8G8R8A8_UNORM,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          tempCmdBuffer);

    // Copiar a imagem para o buffer
    CopyImageToBuffer(image, stagingBuffer, extent, tempCmdBuffer);

    // Restaurar o layout da imagem para apresentação
    TransitionImageLayout(image, VK_FORMAT_B8G8R8A8_UNORM,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          tempCmdBuffer);

    endSingleTimeCommands(tempCmdBuffer);

    // Mapear o buffer e capturar os dados
    void* data;
    vkMapMemory(g_device, stagingBufferMemory, 0, imageSize, 0, &data);

    if (currentFrame.pixels) {
        if (lastFrame.pixels) free(lastFrame.pixels);
        lastFrame = currentFrame;
    }
    currentFrame.image  = image;
    currentFrame.width  = extent.width;
    currentFrame.height = extent.height;
    currentFrame.pixels = (uint8_t*)malloc(imageSize);
    memcpy(currentFrame.pixels, data, imageSize);

    vkUnmapMemory(g_device, stagingBufferMemory);
    vkDestroyBuffer(g_device, stagingBuffer, nullptr);
    vkFreeMemory(g_device, stagingBufferMemory, nullptr);

    // Se tivermos dois frames e o Omg.so estiver carregado, chamar o algoritmo "omg"
    if (pGenerateOmgFrame && lastFrame.pixels && currentFrame.pixels) {
        uint8_t* outputPixels = (uint8_t*)malloc(imageSize);
        pGenerateOmgFrame(0.5f, lastFrame.pixels, currentFrame.pixels,
                          extent.width, extent.height, outputPixels);

        // --- Novo: Copiar outputPixels para a imagem Vulkan ---
        VkBuffer interpolatedStagingBuffer;
        VkDeviceMemory interpolatedStagingBufferMemory;

        CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            interpolatedStagingBuffer, interpolatedStagingBufferMemory);

        void* data;
        vkMapMemory(g_device, interpolatedStagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, outputPixels, static_cast<size_t>(imageSize));
        vkUnmapMemory(g_device, interpolatedStagingBufferMemory);

        VkCommandBuffer copyCmdBuffer = beginSingleTimeCommands();

        TransitionImageLayout(g_interpolatedImage, VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copyCmdBuffer);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { extent.width, extent.height, 1 };

        vkCmdCopyBufferToImage(copyCmdBuffer, interpolatedStagingBuffer, g_interpolatedImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        TransitionImageLayout(g_interpolatedImage, VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, copyCmdBuffer);

        endSingleTimeCommands(copyCmdBuffer);

        vkDestroyBuffer(g_device, interpolatedStagingBuffer, nullptr);
        vkFreeMemory(g_device, interpolatedStagingBufferMemory, nullptr);
        free(outputPixels);
    }
}

// ================================
// FUNÇÕES AUXILIARES DO VULKAN (stubs/simplificados)
// ================================
void createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "Hydra Overlay";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "Hydra Engine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_1;
    
    const char* extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME };
    
    VkInstanceCreateInfo createInfo{};
    createInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;
    
    if (vkCreateInstance(&createInfo, nullptr, &g_instance) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar instância Vulkan!");
}

void createSurface() {
    if (xDisplay == nullptr || targetWindow == 0)
        throw std::runtime_error("Display X11 ou janela alvo não definidos.");
    
    VkXlibSurfaceCreateInfoKHR createInfo{};
    createInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    createInfo.dpy    = xDisplay;
    createInfo.window = targetWindow;
    
    if (vkCreateXlibSurfaceKHR(g_instance, &createInfo, nullptr, &g_surface) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar superfície Xlib!");
}

void pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(g_instance, &deviceCount, nullptr);
    if (deviceCount == 0)
        throw std::runtime_error("Falha ao encontrar GPUs com suporte a Vulkan!");
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(g_instance, &deviceCount, devices.data());
    
    g_physicalDevice = devices[0];
    if (g_physicalDevice == VK_NULL_HANDLE)
        throw std::runtime_error("Falha ao encontrar um dispositivo físico adequado!");
}

void findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    
    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphicsFamily = i;
        
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, g_surface, &presentSupport);
        if (presentSupport)
            indices.presentFamily = i;
        
        if (indices.isComplete())
            break;
        i++;
    }
    
    if (!indices.isComplete())
        throw std::runtime_error("Falha ao encontrar famílias de filas adequadas!");
    
    g_graphicsQueueFamily = indices.graphicsFamily.value();
    g_presentQueueFamily  = indices.presentFamily.value();
}

void createLogicalDevice() {
    findQueueFamilies(g_physicalDevice);
    
    std::set<uint32_t> uniqueQueueFamilies = { g_graphicsQueueFamily, g_presentQueueFamily };
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount       = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueInfo);
    }
    
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    deviceInfo.pQueueCreateInfos    = queueCreateInfos.data();
    
    const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    
    if (vkCreateDevice(g_physicalDevice, &deviceInfo, nullptr, &g_device) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar dispositivo lógico!");
    
    vkGetDeviceQueue(g_device, g_graphicsQueueFamily, 0, &g_graphicsQueue);
    vkGetDeviceQueue(g_device, g_presentQueueFamily, 0, &g_presentQueue);
}

SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device) {
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, g_surface, &details.capabilities);
    
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, g_surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, g_surface, &formatCount, details.formats.data());
    }
    
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, g_surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& format : availableFormats)
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return format;
    return availableFormats[0];
}

VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& mode : availablePresentModes)
        if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
            return mode;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX)
        return capabilities.currentExtent;
    else {
        VkExtent2D actualExtent = {800, 600};
        actualExtent.width  = std::max(capabilities.minImageExtent.width,
                                       std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height,
                                       std::min(capabilities.maxImageExtent.height, actualExtent.height));
        return actualExtent;
    }
}

void createSwapchain() {
    SwapchainSupportDetails support = querySwapchainSupport(g_physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(support.presentModes);
    VkExtent2D extent = chooseSwapExtent(support.capabilities);
    
    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
        imageCount = support.capabilities.maxImageCount;
    
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface          = g_surface;
    createInfo.minImageCount    = imageCount;
    createInfo.imageFormat      = surfaceFormat.format;
    createInfo.imageColorSpace  = surfaceFormat.colorSpace;
    createInfo.imageExtent      = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    
    uint32_t queueFamilyIndices[] = { g_graphicsQueueFamily, g_presentQueueFamily };
    if (g_graphicsQueueFamily != g_presentQueueFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    createInfo.preTransform   = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode    = presentMode;
    createInfo.clipped        = VK_TRUE;
    
    if (vkCreateSwapchainKHR(g_device, &createInfo, nullptr, &g_swapchain) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar swapchain!");
    
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &imageCount, nullptr);
    g_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(g_device, g_swapchain, &imageCount, g_swapchainImages.data());
    
    g_swapchainImageFormat = surfaceFormat.format;
    g_swapchainExtent = extent;
}

void createImageViews() {
    g_swapchainImageViews.resize(g_swapchainImages.size());
    for (size_t i = 0; i < g_swapchainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image    = g_swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format   = g_swapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel     = 0;
        createInfo.subresourceRange.levelCount       = 1;
        createInfo.subresourceRange.baseArrayLayer   = 0;
        createInfo.subresourceRange.layerCount       = 1;
        
        if (vkCreateImageView(g_device, &createInfo, nullptr, &g_swapchainImageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Falha ao criar image views!");
    }
}

void createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = g_swapchainImageFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorAttachmentRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass      = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass      = 0;
    dependency.srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask   = 0;
    dependency.dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments    = &colorAttachment;
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;
    
    if (vkCreateRenderPass(g_device, &renderPassInfo, nullptr, &g_renderPass) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar render pass!");
}

void createGraphicsPipeline() {
    std::vector<char> vertShaderCode = readFile("Shaders/vert.spv");
    std::vector<char> fragShaderCode = readFile("Shaders/frag.spv");
    
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, texCoord);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
    
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName  = "main";
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName  = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };
      
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = (float)g_swapchainExtent.width;
    viewport.height   = (float)g_swapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = g_swapchainExtent;
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports    = &viewport;
    viewportState.scissorCount  = 1;
    viewportState.pScissors     = &scissor;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable        = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode             = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth               = 1.0f;
    rasterizer.cullMode                = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace               = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable         = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable  = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable    = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable   = VK_FALSE;
    colorBlending.logicOp         = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments    = &colorBlendAttachment;
    
    // Criação do Pipeline Layout vinculando o Descriptor Set Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &g_descriptorSetLayout;

    if (vkCreatePipelineLayout(g_device, &pipelineLayoutInfo, nullptr, &g_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Falha ao criar pipeline layout!");
    }
    
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = shaderStages;
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = nullptr;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.layout              = g_pipelineLayout;
    pipelineInfo.renderPass          = g_renderPass;
    pipelineInfo.subpass             = 0;
    
    if (vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &g_graphicsPipeline) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar pipeline gráfico!");
    
    vkDestroyShaderModule(g_device, vertShaderModule, nullptr);
    vkDestroyShaderModule(g_device, fragShaderModule, nullptr);
}

void createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = g_graphicsQueueFamily;
    if (vkCreateCommandPool(g_device, &poolInfo, nullptr, &g_commandPool) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar command pool!");
}

void createFramebuffers() {
    g_framebuffers.resize(g_swapchainImageViews.size());
    for (size_t i = 0; i < g_swapchainImageViews.size(); i++) {
        VkImageView attachments[] = { g_swapchainImageViews[i] };
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType      = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = g_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width  = g_swapchainExtent.width;
        framebufferInfo.height = g_swapchainExtent.height;
        framebufferInfo.layers = 1;
        if (vkCreateFramebuffer(g_device, &framebufferInfo, nullptr, &g_framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Falha ao criar framebuffer!");
    }
}

void createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(g_device, &semaphoreInfo, nullptr, &g_imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(g_device, &semaphoreInfo, nullptr, &g_renderFinishedSemaphore) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar semáforos!");
}

// Stub para readFile (leitura de arquivos binários)
std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("Falha ao abrir o arquivo: " + filename);
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

// Stub para createShaderModule
VkShaderModule createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(g_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar módulo de shader!");
    return shaderModule;
}

// Stub para recreateSwapchain
void recreateSwapchain() {
    vkDeviceWaitIdle(g_device);
    cleanupSwapchain();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
}

// Stub para cleanupSwapchain
void cleanupSwapchain() {
    for (auto framebuffer : g_framebuffers)
        vkDestroyFramebuffer(g_device, framebuffer, nullptr);
    for (auto imageView : g_swapchainImageViews)
        vkDestroyImageView(g_device, imageView, nullptr);
    vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
}

// Stub para beginSingleTimeCommands
VkCommandBuffer beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = g_commandPool;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(g_device, &allocInfo, &commandBuffer);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

// Stub para endSingleTimeCommands
void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &commandBuffer;
    vkQueueSubmit(g_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_graphicsQueue);
    vkFreeCommandBuffers(g_device, g_commandPool, 1, &commandBuffer);
}

// ================================
// IMPLEMENTAÇÃO DA FUNÇÃO CreateBuffer (stub)
// ================================
void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  VkBuffer &buffer, VkDeviceMemory &bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateBuffer(g_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
        throw std::runtime_error("Falha ao criar buffer!");
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_device, buffer, &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);
    
    if (vkAllocateMemory(g_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
        throw std::runtime_error("Falha ao alocar memória para buffer!");
    
    vkBindBufferMemory(g_device, buffer, bufferMemory, 0);
}

// ================================
// IMPLEMENTAÇÃO DA FUNÇÃO FindMemoryType (stub)
// ================================
uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(g_physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw std::runtime_error("Falha ao encontrar o tipo de memória adequado!");
}

// Stub para TransitionImageLayout
void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout,
                           VkImageLayout newLayout, VkCommandBuffer commandBuffer) {
    VkImageMemoryBarrier barrier{};
    barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout                       = oldLayout;
    barrier.newLayout                       = newLayout;
    barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
    barrier.image                           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel     = 0;
    barrier.subresourceRange.levelCount       = 1;
    barrier.subresourceRange.baseArrayLayer   = 0;
    barrier.subresourceRange.layerCount       = 1;
    
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;
    
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

// Stub para CopyImageToBuffer
void CopyImageToBuffer(VkImage image, VkBuffer buffer, VkExtent2D extent, VkCommandBuffer commandBuffer) {
    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount     = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = { extent.width, extent.height, 1 };
    
    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &region);
}
