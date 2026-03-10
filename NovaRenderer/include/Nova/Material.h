/**
 * Nova Renderer - Material System
 * 材质系统 - 支持 PBR 材质、材质实例、材质排序和排序渲染
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Nova {

class VulkanBackend;

// ============================================================================
// 材质属性
// ============================================================================

// 材质属性类型
enum class MaterialPropertyType : u32 {
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Int2,
    Int3,
    Int4,
    Matrix,
    Texture2D,
    TextureCube,
    Texture2DArray
};

// 材质属性值
struct MaterialPropertyValue {
    MaterialPropertyType type = MaterialPropertyType::Float;
    union {
        f32 floatValue = 0;
        Vec2 float2Value;
        Vec3 float3Value;
        Vec4 float4Value;
        i32 intValue;
        Vec2i int2Value;
        Vec3i int3Value;
        Vec4i int4Value;
    };
    TextureHandle textureValue;
    Mat4 matrixValue;
    
    MaterialPropertyValue() : floatValue(0) {}
};

// 材质属性定义
struct MaterialPropertyDef {
    std::string name;
    MaterialPropertyType type;
    MaterialPropertyValue defaultValue;
    u32 offset;  // 在常量缓冲中的偏移
    u32 size;    // 大小 (字节)
    u32 binding; // 绑定点
    std::string group; // 属性组
};

// ============================================================================
// 材质模板
// ============================================================================

// 材质模板
class MaterialTemplate {
public:
    MaterialTemplate();
    ~MaterialTemplate();
    
    // 初始化
    bool initialize(VulkanBackend* backend, const std::string& name);
    void shutdown();
    
    // 属性定义
    void defineProperty(const std::string& name, MaterialPropertyType type,
                        const MaterialPropertyValue& defaultValue,
                        const std::string& group = "default");
    
    void defineTexture(const std::string& name, u32 binding,
                       TextureHandle defaultTexture = {},
                       const std::string& group = "default");
    
    // 着色器
    void setShader(ShaderHandle shader);
    void setShaders(ShaderHandle vertex, ShaderHandle fragment);
    
    // 管线
    void setPipeline(PipelineHandle pipeline);
    PipelineHandle getPipeline() const { return pipeline_; }
    
    // 获取属性定义
    const MaterialPropertyDef* getPropertyDef(const std::string& name) const;
    const std::vector<MaterialPropertyDef>& getPropertyDefs() const { return propertyDefs_; }
    
    // 获取属性布局
    u32 getPropertyBufferSize() const { return propertyBufferSize_; }
    
    // 名称
    const std::string& getName() const { return name_; }
    
private:
    VulkanBackend* backend_ = nullptr;
    std::string name_;
    
    std::vector<MaterialPropertyDef> propertyDefs_;
    std::unordered_map<std::string, u32> propertyIndex_;
    
    ShaderHandle shader_;
    PipelineHandle pipeline_;
    
    u32 propertyBufferSize_ = 0;
};

// ============================================================================
// 材质实例
// ============================================================================

// 材质实例
class MaterialInstance {
public:
    MaterialInstance();
    ~MaterialInstance();
    
    // 初始化
    bool initialize(MaterialTemplate* templ);
    void shutdown();
    
    // 设置属性
    void setFloat(const std::string& name, f32 value);
    void setFloat2(const std::string& name, const Vec2& value);
    void setFloat3(const std::string& name, const Vec3& value);
    void setFloat4(const std::string& name, const Vec4& value);
    void setInt(const std::string& name, i32 value);
    void setMatrix(const std::string& name, const Mat4& value);
    void setTexture(const std::string& name, TextureHandle texture);
    
    // 获取属性
    f32 getFloat(const std::string& name) const;
    Vec2 getFloat2(const std::string& name) const;
    Vec3 getFloat3(const std::string& name) const;
    Vec4 getFloat4(const std::string& name) const;
    i32 getInt(const std::string& name) const;
    Mat4 getMatrix(const std::string& name) const;
    TextureHandle getTexture(const std::string& name) const;
    
    // 获取模板
    MaterialTemplate* getTemplate() const { return template_; }
    
    // 获取属性缓冲
    BufferHandle getPropertyBuffer() const { return propertyBuffer_; }
    const std::vector<TextureHandle>& getTextures() const { return textures_; }
    
    // 更新 GPU 数据
    void updateGPUData();
    
    // 绑定到管线
    void bind(VkCommandBuffer cmd);
    
    // 脏标志
    bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    
private:
    MaterialTemplate* template_ = nullptr;
    
    std::vector<u8> propertyData_;
    std::vector<TextureHandle> textures_;
    BufferHandle propertyBuffer_;
    
    std::unordered_map<std::string, u32> propertyOffsets_;
    
    bool dirty_ = true;
};

// ============================================================================
// PBR 材质
// ============================================================================

// PBR 材质属性
struct PBRMaterialProperties {
    Vec4 albedoColor = Vec4(1.0f);
    Vec4 emissiveColor = Vec4(0.0f);
    
    f32 metallic = 0.5f;
    f32 roughness = 0.5f;
    f32 ao = 1.0f;
    f32 opacity = 1.0f;
    
    f32 normalScale = 1.0f;
    f32 emissiveIntensity = 1.0f;
    f32 ior = 1.5f;  // 折射率
    f32 transmission = 0.0f;
    
    // 纹理
    TextureHandle albedoMap;
    TextureHandle normalMap;
    TextureHandle metallicRoughnessMap;
    TextureHandle aoMap;
    TextureHandle emissiveMap;
    TextureHandle heightMap;
};

// PBR 材质
class PBRMaterial {
public:
    PBRMaterial();
    ~PBRMaterial();
    
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 设置属性
    void setAlbedoColor(const Vec4& color);
    void setEmissiveColor(const Vec4& color);
    void setMetallic(f32 value);
    void setRoughness(f32 value);
    void setAO(f32 value);
    void setOpacity(f32 value);
    
    // 设置纹理
    void setAlbedoMap(TextureHandle texture);
    void setNormalMap(TextureHandle texture);
    void setMetallicRoughnessMap(TextureHandle texture);
    void setAOMap(TextureHandle texture);
    void setEmissiveMap(TextureHandle texture);
    
    // 获取材质实例
    MaterialInstance* getInstance() const { return instance_.get(); }
    
    // 获取属性
    const PBRMaterialProperties& getProperties() const { return properties_; }
    
private:
    VulkanBackend* backend_ = nullptr;
    PBRMaterialProperties properties_;
    std::unique_ptr<MaterialInstance> instance_;
};

// ============================================================================
// 材质排序
// ============================================================================

// 材质排序键
struct MaterialSortKey {
    u64 key;
    MaterialInstance* material;
    
    bool operator<(const MaterialSortKey& o) const {
        return key < o.key;
    }
};

// 材质排序器
class MaterialSorter {
public:
    // 生成排序键
    static u64 generateKey(MaterialInstance* material, const Vec3& cameraPosition);
    
    // 排序材质
    static void sort(std::vector<MaterialSortKey>& keys);
    
    // 排序并去重
    static void sortAndDeduplicate(std::vector<MaterialSortKey>& keys);
};

// ============================================================================
// 材质缓存
// ============================================================================

// 材质缓存
class MaterialCache {
public:
    MaterialCache();
    ~MaterialCache();
    
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 创建材质模板
    MaterialTemplate* createTemplate(const std::string& name);
    MaterialTemplate* getTemplate(const std::string& name);
    
    // 创建材质实例
    MaterialInstance* createInstance(const std::string& templateName);
    MaterialInstance* createInstance(MaterialTemplate* templ);
    
    // 创建 PBR 材质
    PBRMaterial* createPBRMaterial();
    
    // 释放
    void releaseInstance(MaterialInstance* instance);
    void releaseTemplate(MaterialTemplate* templ);
    
    // 清空
    void clear();
    
    // 统计
    u32 getTemplateCount() const;
    u32 getInstanceCount() const;
    
private:
    VulkanBackend* backend_ = nullptr;
    
    std::unordered_map<std::string, std::unique_ptr<MaterialTemplate>> templates_;
    std::vector<std::unique_ptr<MaterialInstance>> instances_;
    std::vector<std::unique_ptr<PBRMaterial>> pbrMaterials_;
};

// ============================================================================
// 内置材质
// ============================================================================

// 内置材质类型
enum class BuiltinMaterial : u32 {
    Unlit,              // 无光照
    UnlitTextured,      // 无光照 + 纹理
    PBR,                // PBR
    PBRTextured,        // PBR + 纹理
    SolidColor,         // 纯色
    Wireframe,          // 线框
    Normal,             // 法线可视化
    Depth,              // 深度可视化
    Sprite,             // 精灵
    Particle,           // 粒子
    PostProcess,        // 后处理
    UI,                 // UI
    RoundedRect,        // 圆角矩形
    Text                // 文字
};

// 内置材质管理器
class BuiltinMaterials {
public:
    BuiltinMaterials();
    ~BuiltinMaterials();
    
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 获取内置材质
    MaterialTemplate* getMaterial(BuiltinMaterial type);
    MaterialInstance* createInstance(BuiltinMaterial type);
    
private:
    bool createBuiltinTemplates();
    
    VulkanBackend* backend_ = nullptr;
    std::unordered_map<BuiltinMaterial, std::unique_ptr<MaterialTemplate>> templates_;
};

} // namespace Nova
