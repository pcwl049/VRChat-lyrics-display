/**
 * Nova Renderer - Shader Hot Reload Implementation
 */

#include "Nova/ShaderHotReload.h"
#include "Nova/VulkanBackend.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Nova {

// ============================================================================
// FileWatcher Implementation
// ============================================================================

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() = default;

void FileWatcher::addWatch(const std::string& path, u32 shaderId) {
    if (!std::filesystem::exists(path)) return;
    
    FileWatchInfo info;
    info.path = path;
    info.lastModified = std::filesystem::last_write_time(path);
    info.shaderId = shaderId;
    
    watches_[path] = info;
}

void FileWatcher::removeWatch(const std::string& path) {
    watches_.erase(path);
}

std::vector<std::pair<std::string, u32>> FileWatcher::checkChanges() {
    std::vector<std::pair<std::string, u32>> changed;
    
    for (auto& [path, info] : watches_) {
        if (!std::filesystem::exists(path)) continue;
        
        auto currentTime = std::filesystem::last_write_time(path);
        if (currentTime != info.lastModified) {
            info.lastModified = currentTime;
            changed.emplace_back(path, info.shaderId);
        }
    }
    
    return changed;
}

void FileWatcher::clear() {
    watches_.clear();
}

// ============================================================================
// ShaderHotReloader Implementation
// ============================================================================

ShaderHotReloader::ShaderHotReloader() = default;

ShaderHotReloader::~ShaderHotReloader() {
    shutdown();
}

bool ShaderHotReloader::initialize(VulkanBackend* backend) {
    backend_ = backend;
    lastCheck_ = std::chrono::steady_clock::now();
    return true;
}

void ShaderHotReloader::shutdown() {
    shaderSources_.clear();
    fileToShaders_.clear();
    fileWatcher_.clear();
    backend_ = nullptr;
}

void ShaderHotReloader::registerShader(ShaderHandle shader, const ShaderSourceInfo& sourceInfo) {
    if (!shader.valid()) return;
    
    shaderSources_[shader.index] = sourceInfo;
    
    // 添加文件监视
    if (!sourceInfo.vertexPath.empty()) {
        fileWatcher_.addWatch(sourceInfo.vertexPath, shader.index);
        fileToShaders_[sourceInfo.vertexPath].insert(shader.index);
    }
    if (!sourceInfo.fragmentPath.empty()) {
        fileWatcher_.addWatch(sourceInfo.fragmentPath, shader.index);
        fileToShaders_[sourceInfo.fragmentPath].insert(shader.index);
    }
    if (!sourceInfo.computePath.empty()) {
        fileWatcher_.addWatch(sourceInfo.computePath, shader.index);
        fileToShaders_[sourceInfo.computePath].insert(shader.index);
    }
    if (!sourceInfo.geometryPath.empty()) {
        fileWatcher_.addWatch(sourceInfo.geometryPath, shader.index);
        fileToShaders_[sourceInfo.geometryPath].insert(shader.index);
    }
    
    // 添加包含文件监视
    for (const auto& include : sourceInfo.includes) {
        fileWatcher_.addWatch(include, shader.index);
        fileToShaders_[include].insert(shader.index);
    }
}

void ShaderHotReloader::unregisterShader(ShaderHandle shader) {
    auto it = shaderSources_.find(shader.index);
    if (it == shaderSources_.end()) return;
    
    const auto& info = it->second;
    
    // 移除文件监视
    if (!info.vertexPath.empty()) {
        fileWatcher_.removeWatch(info.vertexPath);
        fileToShaders_.erase(info.vertexPath);
    }
    if (!info.fragmentPath.empty()) {
        fileWatcher_.removeWatch(info.fragmentPath);
        fileToShaders_.erase(info.fragmentPath);
    }
    if (!info.computePath.empty()) {
        fileWatcher_.removeWatch(info.computePath);
        fileToShaders_.erase(info.computePath);
    }
    if (!info.geometryPath.empty()) {
        fileWatcher_.removeWatch(info.geometryPath);
        fileToShaders_.erase(info.geometryPath);
    }
    
    shaderSources_.erase(it);
}

void ShaderHotReloader::update() {
    if (!enabled_) return;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheck_).count();
    
    if (elapsed < checkIntervalMs_) return;
    lastCheck_ = now;
    
    // 检查文件变更
    auto changed = fileWatcher_.checkChanges();
    
    // 重载变更的着色器
    std::unordered_set<u32> shadersToReload;
    for (const auto& [path, shaderId] : changed) {
        shadersToReload.insert(shaderId);
    }
    
    for (u32 shaderId : shadersToReload) {
        ShaderHandle shader{shaderId};
        reloadShader(shader);
    }
}

bool ShaderHotReloader::reloadShader(ShaderHandle shader) {
    auto it = shaderSources_.find(shader.index);
    if (it == shaderSources_.end()) return false;
    
    return compileShader(shader, it->second);
}

void ShaderHotReloader::reloadAll() {
    for (const auto& [id, info] : shaderSources_) {
        compileShader(ShaderHandle{id}, info);
    }
}

bool ShaderHotReloader::compileShader(ShaderHandle shader, const ShaderSourceInfo& sourceInfo) {
    ShaderDesc desc;
    
    // 加载着色器源码
    if (!sourceInfo.vertexPath.empty()) {
        auto result = ShaderCompiler::compileFromFile(
            sourceInfo.vertexPath,
            VK_SHADER_STAGE_VERTEX_BIT,
            ShaderCompileOptions{sourceInfo.defines}
        );
        if (!result.success) return false;
        desc.vertexSource = "";  // SPIR-V binary 方式
        // 设置 SPIR-V 二进制...
    }
    
    if (!sourceInfo.fragmentPath.empty()) {
        auto result = ShaderCompiler::compileFromFile(
            sourceInfo.fragmentPath,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            ShaderCompileOptions{sourceInfo.defines}
        );
        if (!result.success) return false;
        desc.fragmentSource = "";
    }
    
    if (!sourceInfo.computePath.empty()) {
        auto result = ShaderCompiler::compileFromFile(
            sourceInfo.computePath,
            VK_SHADER_STAGE_COMPUTE_BIT,
            ShaderCompileOptions{sourceInfo.defines}
        );
        if (!result.success) return false;
        desc.computeSource = "";
    }
    
    // 创建新着色器
    ShaderHandle newShader = backend_->createShader(desc);
    if (!newShader.valid()) return false;
    
    // 调用回调
    if (reloadCallback_) {
        reloadCallback_(shader, newShader);
    }
    
    // 销毁旧着色器
    backend_->destroyShader(shader);
    
    // 更新注册
    unregisterShader(shader);
    registerShader(newShader, sourceInfo);
    
    return true;
}

void ShaderHotReloader::updateDependencies(ShaderHandle shader, const std::string& path) {
    auto it = shaderSources_.find(shader.index);
    if (it == shaderSources_.end()) return;
    
    // 更新包含文件列表
    auto& includes = it->second.includes;
    if (std::find(includes.begin(), includes.end(), path) == includes.end()) {
        includes.push_back(path);
        fileWatcher_.addWatch(path, shader.index);
        fileToShaders_[path].insert(shader.index);
    }
}

// ============================================================================
// ShaderCompiler Implementation
// ============================================================================

ShaderCompileResult ShaderCompiler::compileGLSL(
    const std::string& source,
    VkShaderStageFlagBits stage,
    const ShaderCompileOptions& options) {
    
    ShaderCompileResult result;
    
    // 预处理
    std::string processed = preprocess(source, options, result.includes);
    
    // 编译 GLSL 到 SPIR-V
    // 使用 glslang 编译器
    // ...
    
    result.success = true;
    return result;
}

ShaderCompileResult ShaderCompiler::compileHLSL(
    const std::string& source,
    VkShaderStageFlagBits stage,
    const ShaderCompileOptions& options) {
    
    ShaderCompileResult result;
    
    // HLSL 编译逻辑
    // 使用 dxc 或 glslang HLSL 前端
    // ...
    
    return result;
}

ShaderCompileResult ShaderCompiler::compileFromFile(
    const std::string& path,
    VkShaderStageFlagBits stage,
    const ShaderCompileOptions& options) {
    
    ShaderCompileResult result;
    
    std::string source = loadFile(path);
    if (source.empty()) {
        result.errorMessage = "Failed to load shader file: " + path;
        return result;
    }
    
    // 处理包含路径
    ShaderCompileOptions modifiedOptions = options;
    modifiedOptions.includePath = std::filesystem::path(path).parent_path().string();
    
    return compileGLSL(source, stage, modifiedOptions);
}

std::string ShaderCompiler::preprocess(
    const std::string& source,
    const ShaderCompileOptions& options,
    std::vector<std::string>& outIncludes) {
    
    std::string result = source;
    
    // 添加宏定义
    if (!options.defines.empty()) {
        std::string defines;
        for (const auto& [name, value] : options.defines) {
            defines += "#define " + name + " " + value + "\n";
        }
        result = "#version 450\n" + defines + result;
    }
    
    // 处理 #include 指令
    result = processIncludes(result, options.includePath, outIncludes);
    
    return result;
}

bool ShaderCompiler::reflect(
    const std::vector<u32>& spirv,
    std::vector<ShaderResource>& resources) {
    
    return SpirvReflector::reflect(spirv, resources);
}

std::string ShaderCompiler::loadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string ShaderCompiler::processIncludes(
    const std::string& source,
    const std::string& basePath,
    std::vector<std::string>& includes) {
    
    std::string result;
    std::istringstream stream(source);
    std::string line;
    
    while (std::getline(stream, line)) {
        // 查找 #include
        size_t includePos = line.find("#include");
        if (includePos != std::string::npos) {
            // 提取文件名
            size_t start = line.find('"');
            if (start == std::string::npos) {
                start = line.find('<');
            }
            size_t end = line.find_last_of("\"'>");
            
            if (start != std::string::npos && end != std::string::npos) {
                std::string includeFile = line.substr(start + 1, end - start - 1);
                std::string includePath = basePath + "/" + includeFile;
                
                if (std::find(includes.begin(), includes.end(), includePath) == includes.end()) {
                    includes.push_back(includePath);
                    
                    // 递归处理包含文件
                    std::string includeContent = loadFile(includePath);
                    includeContent = processIncludes(includeContent, basePath, includes);
                    result += includeContent + "\n";
                }
                continue;
            }
        }
        
        result += line + "\n";
    }
    
    return result;
}

// ============================================================================
// ShaderCache Implementation
// ============================================================================

ShaderCache::ShaderCache() = default;

ShaderCache::~ShaderCache() {
    shutdown();
}

bool ShaderCache::initialize(const std::string& cachePath) {
    cachePath_ = cachePath;
    return load();
}

void ShaderCache::shutdown() {
    if (dirty_) {
        save();
    }
    entries_.clear();
}

bool ShaderCache::find(const std::string& sourceHash, ShaderCacheEntry& entry) {
    auto it = entries_.find(sourceHash);
    if (it != entries_.end()) {
        entry = it->second;
        return true;
    }
    return false;
}

void ShaderCache::store(const std::string& sourceHash, const ShaderCacheEntry& entry) {
    entries_[sourceHash] = entry;
    dirty_ = true;
}

void ShaderCache::clear() {
    entries_.clear();
    dirty_ = true;
}

void ShaderCache::save() {
    if (cachePath_.empty()) return;
    
    std::ofstream file(cachePath_, std::ios::binary);
    if (!file.is_open()) return;
    
    // 写入条目数量
    u32 count = static_cast<u32>(entries_.size());
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    // 写入每个条目
    for (const auto& [hash, entry] : entries_) {
        // 写入哈希
        u32 hashLen = static_cast<u32>(hash.size());
        file.write(reinterpret_cast<const char*>(&hashLen), sizeof(hashLen));
        file.write(hash.data(), hashLen);
        
        // 写入 SPIR-V 二进制大小
        u32 spirvSize = static_cast<u32>(entry.spirvBinary.size() * sizeof(u32));
        file.write(reinterpret_cast<const char*>(&spirvSize), sizeof(spirvSize));
        file.write(reinterpret_cast<const char*>(entry.spirvBinary.data()), spirvSize);
        
        // 写入时间戳
        file.write(reinterpret_cast<const char*>(&entry.timestamp), sizeof(entry.timestamp));
    }
    
    dirty_ = false;
}

bool ShaderCache::load() {
    if (cachePath_.empty()) return false;
    
    std::ifstream file(cachePath_, std::ios::binary);
    if (!file.is_open()) return false;
    
    // 读取条目数量
    u32 count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    for (u32 i = 0; i < count; i++) {
        // 读取哈希
        u32 hashLen;
        file.read(reinterpret_cast<char*>(&hashLen), sizeof(hashLen));
        
        std::string hash(hashLen, '\0');
        file.read(hash.data(), hashLen);
        
        // 读取 SPIR-V 二进制
        u32 spirvSize;
        file.read(reinterpret_cast<char*>(&spirvSize), sizeof(spirvSize));
        
        ShaderCacheEntry entry;
        entry.spirvBinary.resize(spirvSize / sizeof(u32));
        file.read(reinterpret_cast<char*>(entry.spirvBinary.data()), spirvSize);
        
        // 读取时间戳
        file.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));
        
        entries_[hash] = entry;
    }
    
    return true;
}

u64 ShaderCache::getCacheSize() const {
    u64 size = 0;
    for (const auto& [hash, entry] : entries_) {
        size += entry.spirvBinary.size() * sizeof(u32);
    }
    return size;
}

// ============================================================================
// ShaderVariantManager Implementation
// ============================================================================

ShaderVariantManager::ShaderVariantManager() = default;

ShaderVariantManager::~ShaderVariantManager() {
    shutdown();
}

bool ShaderVariantManager::initialize(VulkanBackend* backend) {
    backend_ = backend;
    return true;
}

void ShaderVariantManager::shutdown() {
    clearAll();
    backend_ = nullptr;
}

ShaderHandle ShaderVariantManager::getVariant(
    ShaderHandle baseShader,
    const std::unordered_map<std::string, std::string>& defines) {
    
    ShaderVariantKey key;
    key.baseShader = baseShader;
    key.defines = defines;
    
    auto it = variants_.find(key);
    if (it != variants_.end()) {
        return it->second;
    }
    
    // 创建新变体
    // ...
    
    return {};
}

void ShaderVariantManager::precompileVariants(
    ShaderHandle baseShader,
    const std::vector<std::unordered_map<std::string, std::string>>& variantDefs) {
    
    for (const auto& defines : variantDefs) {
        getVariant(baseShader, defines);
    }
}

void ShaderVariantManager::clearVariants(ShaderHandle baseShader) {
    for (auto it = variants_.begin(); it != variants_.end();) {
        if (it->first.baseShader == baseShader) {
            if (backend_ && it->second.valid()) {
                backend_->destroyShader(it->second);
            }
            it = variants_.erase(it);
        } else {
            ++it;
        }
    }
}

void ShaderVariantManager::clearAll() {
    for (auto& [key, shader] : variants_) {
        if (backend_ && shader.valid()) {
            backend_->destroyShader(shader);
        }
    }
    variants_.clear();
}

u32 ShaderVariantManager::getVariantCount(ShaderHandle baseShader) const {
    u32 count = 0;
    for (const auto& [key, shader] : variants_) {
        if (key.baseShader == baseShader) count++;
    }
    return count;
}

u32 ShaderVariantManager::getTotalVariantCount() const {
    return static_cast<u32>(variants_.size());
}

// ============================================================================
// SpirvReflector Implementation
// ============================================================================

bool SpirvReflector::reflect(
    const std::vector<u32>& spirv,
    std::vector<ShaderResource>& resources) {
    
    // 使用 SPIRV-Reflect 进行反射
    // 这里简化实现
    
    return true;
}

bool SpirvReflector::reflectInputs(
    const std::vector<u32>& spirv,
    std::vector<std::pair<u32, std::string>>& inputs) {
    
    return true;
}

bool SpirvReflector::reflectOutputs(
    const std::vector<u32>& spirv,
    std::vector<std::pair<u32, std::string>>& outputs) {
    
    return true;
}

bool SpirvReflector::reflectPushConstants(
    const std::vector<u32>& spirv,
    std::vector<ShaderResource>& pushConstants) {
    
    return true;
}

u32 SpirvReflector::getDescriptorSetCount(const std::vector<u32>& spirv) {
    return 4;  // 简化
}

} // namespace Nova
