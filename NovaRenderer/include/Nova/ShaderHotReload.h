/**
 * Nova Renderer - Shader Hot Reload
 * 着色器热重载系统
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <filesystem>
#include <chrono>

namespace Nova {

class VulkanBackend;

// ============================================================================
// SPIR-V 资源信息
// ============================================================================

// 资源信息
struct ShaderResource {
    enum class Type : u32 {
        UniformBuffer,
        StorageBuffer,
        SampledImage,
        StorageImage,
        Sampler,
        InputAttachment,
        PushConstant,
        SpecializationConstant
    };
    
    Type type;
    std::string name;
    u32 set;
    u32 binding;
    u32 location;
    u32 size;
    u32 arraySize;
    
    // 成员信息 (用于 uniform buffer)
    struct Member {
        std::string name;
        u32 offset;
        u32 size;
    };
    std::vector<Member> members;
};

// ============================================================================
// 着色器文件监视器
// ============================================================================

struct FileWatchInfo {
    std::string path;
    std::filesystem::file_time_type lastModified;
    u32 shaderId;
};

// 文件监视器
class FileWatcher {
public:
    FileWatcher();
    ~FileWatcher();
    
    // 添加监视
    void addWatch(const std::string& path, u32 shaderId);
    void removeWatch(const std::string& path);
    
    // 检查变更
    std::vector<std::pair<std::string, u32>> checkChanges();
    
    // 清空
    void clear();
    
private:
    std::unordered_map<std::string, FileWatchInfo> watches_;
};

// ============================================================================
// 着色器热重载器
// ============================================================================

// 重载回调
using ShaderReloadCallback = std::function<void(ShaderHandle oldShader, ShaderHandle newShader)>;

// 着色器源信息
struct ShaderSourceInfo {
    std::string vertexPath;
    std::string fragmentPath;
    std::string computePath;
    std::string geometryPath;
    
    // 包含的文件 (用于依赖追踪)
    std::vector<std::string> includes;
    
    // 宏定义
    std::unordered_map<std::string, std::string> defines;
};

// 着色器热重载器
class ShaderHotReloader {
public:
    ShaderHotReloader();
    ~ShaderHotReloader();
    
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 注册着色器
    void registerShader(ShaderHandle shader, const ShaderSourceInfo& sourceInfo);
    void unregisterShader(ShaderHandle shader);
    
    // 每帧检查
    void update();
    
    // 手动重载
    bool reloadShader(ShaderHandle shader);
    void reloadAll();
    
    // 设置回调
    void setReloadCallback(ShaderReloadCallback callback) { reloadCallback_ = callback; }
    
    // 启用/禁用
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    // 设置检查间隔 (毫秒)
    void setCheckInterval(u32 ms) { checkIntervalMs_ = ms; }
    
private:
    bool compileShader(ShaderHandle shader, const ShaderSourceInfo& sourceInfo);
    void updateDependencies(ShaderHandle shader, const std::string& path);
    
    VulkanBackend* backend_ = nullptr;
    
    std::unordered_map<u32, ShaderSourceInfo> shaderSources_;
    std::unordered_map<std::string, std::unordered_set<u32>> fileToShaders_;
    
    FileWatcher fileWatcher_;
    ShaderReloadCallback reloadCallback_;
    
    bool enabled_ = true;
    u32 checkIntervalMs_ = 500;
    std::chrono::steady_clock::time_point lastCheck_;
};

// ============================================================================
// 着色器编译器
// ============================================================================

// 编译选项
struct ShaderCompileOptions {
    std::unordered_map<std::string, std::string> defines;
    std::string entryPoint = "main";
    bool optimize = true;
    bool debug = false;
    std::string includePath;
};

// 编译结果
struct ShaderCompileResult {
    bool success = false;
    std::string errorMessage;
    std::vector<u32> spirvBinary;
    std::vector<std::string> includes;
};

// 着色器编译器
class ShaderCompiler {
public:
    static ShaderCompileResult compileGLSL(
        const std::string& source,
        VkShaderStageFlagBits stage,
        const ShaderCompileOptions& options = {}
    );
    
    static ShaderCompileResult compileHLSL(
        const std::string& source,
        VkShaderStageFlagBits stage,
        const ShaderCompileOptions& options = {}
    );
    
    static ShaderCompileResult compileFromFile(
        const std::string& path,
        VkShaderStageFlagBits stage,
        const ShaderCompileOptions& options = {}
    );
    
    // 预处理
    static std::string preprocess(
        const std::string& source,
        const ShaderCompileOptions& options,
        std::vector<std::string>& outIncludes
    );
    
    // 反射
    static bool reflect(
        const std::vector<u32>& spirv,
        std::vector<ShaderResource>& resources
    );
    
private:
    static std::string loadFile(const std::string& path);
    static std::string processIncludes(
        const std::string& source,
        const std::string& basePath,
        std::vector<std::string>& includes
    );
};

// ============================================================================
// 着色器缓存
// ============================================================================

// 缓存项
struct ShaderCacheEntry {
    std::string sourceHash;
    std::vector<u32> spirvBinary;
    std::vector<ShaderResource> resources;
    u64 timestamp;
};

// 着色器缓存
class ShaderCache {
public:
    ShaderCache();
    ~ShaderCache();
    
    bool initialize(const std::string& cachePath);
    void shutdown();
    
    // 查找缓存
    bool find(const std::string& sourceHash, ShaderCacheEntry& entry);
    
    // 存储缓存
    void store(const std::string& sourceHash, const ShaderCacheEntry& entry);
    
    // 清除
    void clear();
    
    // 保存到磁盘
    void save();
    
    // 从磁盘加载
    bool load();
    
    // 统计
    u32 getEntryCount() const { return static_cast<u32>(entries_.size()); }
    u64 getCacheSize() const;
    
private:
    std::string cachePath_;
    std::unordered_map<std::string, ShaderCacheEntry> entries_;
    bool dirty_ = false;
};

// ============================================================================
// 着色器变体
// ============================================================================

// 着色器变体键
struct ShaderVariantKey {
    ShaderHandle baseShader;
    std::unordered_map<std::string, std::string> defines;
    
    bool operator==(const ShaderVariantKey& o) const {
        return baseShader == o.baseShader && defines == o.defines;
    }
};

struct ShaderVariantKeyHash {
    size_t operator()(const ShaderVariantKey& k) const {
        size_t h = std::hash<u32>{}(k.baseShader.index);
        for (const auto& [name, value] : k.defines) {
            h ^= std::hash<std::string>{}(name) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::string>{}(value) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

// 着色器变体管理器
class ShaderVariantManager {
public:
    ShaderVariantManager();
    ~ShaderVariantManager();
    
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 获取变体
    ShaderHandle getVariant(
        ShaderHandle baseShader,
        const std::unordered_map<std::string, std::string>& defines
    );
    
    // 预编译变体
    void precompileVariants(
        ShaderHandle baseShader,
        const std::vector<std::unordered_map<std::string, std::string>>& variantDefs
    );
    
    // 清除变体
    void clearVariants(ShaderHandle baseShader);
    void clearAll();
    
    // 统计
    u32 getVariantCount(ShaderHandle baseShader) const;
    u32 getTotalVariantCount() const;
    
private:
    VulkanBackend* backend_ = nullptr;
    std::unordered_map<ShaderVariantKey, ShaderHandle, ShaderVariantKeyHash> variants_;
};

// ============================================================================
// SPIR-V 反射器
// ============================================================================

class SpirvReflector {
public:
    static bool reflect(
        const std::vector<u32>& spirv,
        std::vector<ShaderResource>& resources
    );
    
    static bool reflectInputs(
        const std::vector<u32>& spirv,
        std::vector<std::pair<u32, std::string>>& inputs
    );
    
    static bool reflectOutputs(
        const std::vector<u32>& spirv,
        std::vector<std::pair<u32, std::string>>& outputs
    );
    
    static bool reflectPushConstants(
        const std::vector<u32>& spirv,
        std::vector<ShaderResource>& pushConstants
    );
    
    static u32 getDescriptorSetCount(const std::vector<u32>& spirv);
};

} // namespace Nova
