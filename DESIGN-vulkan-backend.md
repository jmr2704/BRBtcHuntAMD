# 🔧 Design: Vulkan Compute Backend — BRBtcHuntAMD

## 1. Objetivo

Adicionar suporte a **Vulkan Compute** como backend alternativo ao HIP,
permitindo que GPUs **RDNA 1 (RX 5700 XT, gfx1010)** funcionem no Windows,
onde o driver AMD não oferece HIP/ROCm.

## 2. Arquitetura Geral

```
main.cpp
   │
   ├── Backend::create(type) → BackendHIP ou BackendVulkan
   │
   ├── Backend::init()            ← device discovery
   ├── Backend::getDeviceCount()
   ├── Backend::getDeviceName(i)
   │
   ├── Backend::allocate()        ← device memory
   ├── Backend::memcpyH2D()
   ├── Backend::memcpyD2H()
   ├── Backend::free()
   │
   ├── Backend::launchKernel()    ← compute shader dispatch
   ├── Backend::synchronize()
   └── Backend::waitForEvents()
```

## 3. Interface do Backend (classes abstratas)

```cpp
// Backend.h — Abstract GPU backend interface

enum class BackendType { HIP, Vulkan };

// Device memory handle
struct DeviceMem {
    void* ptr = nullptr;
    size_t size = 0;
};

// Kernel launch parameters
struct LaunchParams {
    uint32_t gridX, gridY, gridZ;
    uint32_t blockX, blockY, blockZ;
    uint32_t sharedMemBytes = 0;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual BackendType type() const = 0;
    virtual bool init() = 0;
    virtual int  getDeviceCount() = 0;
    virtual std::string getDeviceName(int dev) = 0;
    virtual std::string getDeviceArch(int dev) = 0;  // "gfx1032" ou "RDNA2" etc
    
    virtual DeviceMem allocate(int dev, size_t size) = 0;
    virtual void free(DeviceMem mem) = 0;
    virtual bool memcpyH2D(DeviceMem dst, const void* src, size_t size) = 0;
    virtual bool memcpyD2H(void* dst, DeviceMem src, size_t size) = 0;
    
    // Compile shader from SPIR-V or GLSL (Vulkan) / load code object (HIP)
    virtual uint64_t loadShader(const uint8_t* code, size_t size) = 0;
    
    // Launch kernel by shader handle
    virtual bool launch(uint64_t shader, const LaunchParams& lp,
                        const std::vector<DeviceMem>& args) = 0;
    
    virtual bool synchronize(int dev) = 0;
    
    // Factory
    static Backend* create(BackendType type);
};

// BackendFactory function (in Backend.cpp)
Backend* createBackend(BackendType type);
```

## 4. Implementação Vulkan

### 4.1 Arquivos

| Arquivo | Descrição |
|---------|-----------|
| `src/VulkanBackend.cpp` | Implementação da classe Backend para Vulkan |
| `include/VulkanBackend.h` | Declaração da classe VulkanBackend |
| `src/shaders/compile.sh` | Script pra compilar GLSL → SPIR-V (glslangValidator) |
| `src/shaders/sha256.comp` | SHA-256 em GLSL (compute shader) |
| `src/shaders/ripemd160.comp` | RIPEMD-160 em GLSL |
| `src/shaders/hash_pipeline.comp` | Pipeline SHA-256 + RIPEMD-160 completo |
| `src/shaders/ecc_arithmetic.comp` | Aritmética de curva secp256k1 em GLSL |
| `src/shaders/ec_main.comp` | Kernel principal: ECC + hash + match |

### 4.2 Pipeline GLSL

Os shaders serão escritos em **GLSL 4.60** com `#extension GL_EXT_gpu_shader_int64` para suporte a `uint64_t`.

```
ec_main.comp
   ├── globals: push constants (grid params, target hash160 prefix, range)
   ├── storage buffers: start_scalars, Px/Py, found_flag, found_result
   ├── step 1: load scalar → EC point multiplication (in GLSL)
   ├── step 2: SHA-256 do pubkey compressed
   ├── step 3: RIPEMD-160 do hash SHA-256
   └── step 4: compare20 → if match, write found_result
```

### 4.3 Gerenciamento de Dispositivo Vulkan

```cpp
class VulkanBackend : public Backend {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue queue;
    VkCommandPool commandPool;
    VkCommandBuffer cmdBuffer;
    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineCache pipelineCache;
    
    struct ShaderModule {
        VkShaderModule module;
        VkPipeline pipeline;
        VkPipelineLayout layout;
        VkDescriptorSet descriptorSet;
    };
    std::unordered_map<uint64_t, ShaderModule> shaders;
    
    struct DeviceAllocation {
        VkBuffer buffer;
        VkDeviceMemory memory;
        size_t size;
    };
    std::unordered_map<void*, DeviceAllocation> allocations;
};
```

### 4.4 Compilação de Shaders

**Opção A (recomendada):** GLSL compilado offline para SPIR-V.
- Usar `glslangValidator` (parte do Vulkan SDK) durante o build
- SPIR-V embarcado como array `uint32_t` no binário

**Opção B (fallback):** glslang online (linking com shaderc)
- Compilar GLSL em runtime
- Maior dependência, mais flexível

### 4.5 Seleção de Backend

No `main.cpp`:

```cpp
// Auto-detecção do melhor backend
Backend* backend = nullptr;
#if defined(BTC_GPU_BACKEND_VULKAN)
    backend = Backend::create(BackendType::Vulkan);
    if (!backend || !backend->init()) {
        // fallback pra HIP ou erro
    }
#elif defined(BTC_GPU_BACKEND_HIP)
    backend = Backend::create(BackendType::HIP);
#endif
```

No `Makefile`, adicionar um target como:
```makefile
vulkan: HIP_FLAGS_BASE += -DBTC_GPU_BACKEND_VULKAN=1 -DVK_NO_PROTOTYPES
vulkan: clean $(TARGET)
```

Ou usar um script separado `setup-windows-vulkan.ps1` que compila com Vulkan.

## 5. Dependências

| Dependência | Uso |
|-------------|-----|
| **Vulkan SDK** (`vulkan-1.dll`, `vulkan-1.lib`) | Runtime Vulkan |
| **glslangValidator** | Compilar GLSL → SPIR-V (build time) |
| **Vulkan Memory Allocator (VMA)** | Gerenciamento de memória (opcional, mas recomendado) |

O Vulkan SDK já vem com headers, libs e `glslangValidator`.

## 6. Shaders GLSL — Estratégia de Portabilidade

### 6.1 SHA-256 em GLSL

O SHA-256 usa apenas operações de 32 bits (ror, xor, and, add) nativas no GLSL:

```glsl
#extension GL_EXT_gpu_shader_int64 : require

uint ror32(uint x, int n) { return (x >> n) | (x << (32 - n)); }
uvec4 K[64] = uvec4[](/* mesmos valores K */);
void sha256_transform(inout uvec4 state[2], uint W_in[16]) { /* mesma logica */ }
```

### 6.2 RIPEMD-160 em GLSL

Similar ao SHA-256, apenas uint32 operations.

### 6.3 Aritmética de Curva (secp256k1)

A parte mais complexa. Requer:
- `uint64_t` (via `GL_EXT_gpu_shader_int64`)
- Multiplicação 64×64 → 128 bits: `u32vec2 mul64(uint64_t a, uint64_t b)`
- Adição com carry usando `uaddCarry` (GL_EXT_gpu_shader_int64)
- Implementação de modulação Montgomery ou Barret

### 6.4 Wave/Subgroup Operations

Vulkan tem `GL_KHR_shader_subgroup` com:
- `subgroupShuffle` = `__shfl`
- `subgroupShuffleDown` = `__shfl_down`
- `subgroupAny` = `__any`
- `subgroupBallot` = `__ballot`

## 7. Plano de Implementação (Tasks)

1. `[DESIGN]` ← este documento
2. `[SPIKE]` SHA-256 mínimo em Vulkan Compute
3. `[PORT]` Aritmética secp256k1 em GLSL
4. `[PORT]` Pipeline hash completo em GLSL
5. `[PORT]` VulkanBackend.cpp (device, memory, queue, shader loading)
6. `[PORT]` VulkanBackend.h + integração Backend.h no main.cpp
7. `[PORT]` Adaptar found_result, vanity, range splitting
8. `[BENCH]` Benchmark comparativo
9. `[TEST]` Teste na RX 5700 XT (Windows)
10. `[GUIDE]` Documentação + script setup

## 8. Riscos

| Risco | Mitigação |
|-------|-----------|
| GLSL não tem `__shared__` | Usar `shared` (GLSL) com barrier(), suportado em Vulkan |
| Aritmética 256-bit pode ser lenta | Otimizar com operações limb-based e batch inversion |
| Subgroup size varia entre GPUs | Código adaptável ao `gl_SubgroupSize` |
| SPIR-V sem suporte a `uint64_t` em GPUs antigas | Verificar `VK_KHR_shader_float16_int8` / `GL_EXT_gpu_shader_int64` |
