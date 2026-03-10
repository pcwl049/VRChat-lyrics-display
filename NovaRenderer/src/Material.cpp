/**
 * Nova Renderer - Material System Implementation
 */

#include "Nova/Material.h"
#include "Nova/VulkanBackend.h"
#include <algorithm>

namespace Nova {

// ============================================================================
// MaterialTemplate Implementation
// ============================================================================

MaterialTemplate::MaterialTemplate() = default;

MaterialTemplate::~MaterialTemplate() {
    shutdown();
}

bool MaterialTemplate::initialize(VulkanBackend* backend, const std::string& name) {
    backend_ = backend;
    name_ = name;
    return true;
}

void MaterialTemplate::shutdown() {
    propertyDefs_.clear();
    propertyIndex_.clear();
    backend_ = nullptr;
}

void MaterialTemplate::defineProperty(const std::string& name, MaterialPropertyType type,
                                      const MaterialPropertyValue& defaultValue,
                                      const std::string& group) {
    MaterialPropertyDef def;
    def.name = name;
    def.type = type;
    def.defaultValue = defaultValue;
    def.group = group;
    
    // 计算偏移和大小
    switch (type) {
        case MaterialPropertyType::Float:
            def.size = sizeof(f32);
            break;
        case MaterialPropertyType::Float2:
            def.size = sizeof(Vec2);
            break;
        case MaterialPropertyType::Float3:
            def.size = sizeof(Vec3);
            break;
        case MaterialPropertyType::Float4:
            def.size = sizeof(Vec4);
            break;
        case MaterialPropertyType::Int:
            def.size = sizeof(i32);
            break;
        case MaterialPropertyType::Int2:
            def.size = sizeof(Vec2i);
            break;
        case MaterialPropertyType::Int3:
            def.size = sizeof(Vec3i);
            break;
        case MaterialPropertyType::Int4:
            def.size = sizeof(Vec4i);
            break;
        case MaterialPropertyType::Matrix:
            def.size = sizeof(Mat4);
            break;
        case MaterialPropertyType::Texture2D:
        case MaterialPropertyType::TextureCube:
        case MaterialPropertyType::Texture2DArray:
            def.size = 0;  // 纹理不在常量缓冲中
            break;
    }
    
    // 对齐到 16 字节
    def.offset = (propertyBufferSize_ + 15) & ~15;
    propertyBufferSize_ = def.offset + def.size;
    
    u32 index = static_cast<u32>(propertyDefs_.size());
    propertyDefs_.push_back(def);
    propertyIndex_[name] = index;
}

void MaterialTemplate::defineTexture(const std::string& name, u32 binding,
                                     TextureHandle defaultTexture,
                                     const std::string& group) {
    MaterialPropertyDef def;
    def.name = name;
    def.type = MaterialPropertyType::Texture2D;
    def.binding = binding;
    def.defaultValue.textureValue = defaultTexture;
    def.group = group;
    def.size = 0;
    def.offset = 0;
    
    u32 index = static_cast<u32>(propertyDefs_.size());
    propertyDefs_.push_back(def);
    propertyIndex_[name] = index;
}

void MaterialTemplate::setShader(ShaderHandle shader) {
    shader_ = shader;
}

void MaterialTemplate::setShaders(ShaderHandle vertex, ShaderHandle fragment) {
    shader_ = vertex;  // 简化
}

void MaterialTemplate::setPipeline(PipelineHandle pipeline) {
    pipeline_ = pipeline;
}

const MaterialPropertyDef* MaterialTemplate::getPropertyDef(const std::string& name) const {
    auto it = propertyIndex_.find(name);
    if (it != propertyIndex_.end()) {
        return &propertyDefs_[it->second];
    }
    return nullptr;
}

// ============================================================================
// MaterialInstance Implementation
// ============================================================================

MaterialInstance::MaterialInstance() = default;

MaterialInstance::~MaterialInstance() {
    shutdown();
}

bool MaterialInstance::initialize(MaterialTemplate* templ) {
    if (!templ) return false;
    
    template_ = templ;
    
    // 分配属性数据
    u32 bufferSize = templ->getPropertyBufferSize();
    propertyData_.resize(bufferSize, 0);
    
    // 初始化默认值
    for (const auto& def : templ->getPropertyDefs()) {
        propertyOffsets_[def.name] = def.offset;
        
        if (def.size > 0 && def.offset + def.size <= propertyData_.size()) {
            memcpy(propertyData_.data() + def.offset, &def.defaultValue, def.size);
        }
        
        if (def.type == MaterialPropertyType::Texture2D ||
            def.type == MaterialPropertyType::TextureCube ||
            def.type == MaterialPropertyType::Texture2DArray) {
            textures_.push_back(def.defaultValue.textureValue);
        }
    }
    
    return true;
}

void MaterialInstance::shutdown() {
    template_ = nullptr;
    propertyData_.clear();
    textures_.clear();
}

void MaterialInstance::setFloat(const std::string& name, f32 value) {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(f32) <= propertyData_.size()) {
        memcpy(propertyData_.data() + it->second, &value, sizeof(f32));
        dirty_ = true;
    }
}

void MaterialInstance::setFloat2(const std::string& name, const Vec2& value) {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(Vec2) <= propertyData_.size()) {
        memcpy(propertyData_.data() + it->second, &value, sizeof(Vec2));
        dirty_ = true;
    }
}

void MaterialInstance::setFloat3(const std::string& name, const Vec3& value) {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(Vec3) <= propertyData_.size()) {
        memcpy(propertyData_.data() + it->second, &value, sizeof(Vec3));
        dirty_ = true;
    }
}

void MaterialInstance::setFloat4(const std::string& name, const Vec4& value) {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(Vec4) <= propertyData_.size()) {
        memcpy(propertyData_.data() + it->second, &value, sizeof(Vec4));
        dirty_ = true;
    }
}

void MaterialInstance::setInt(const std::string& name, i32 value) {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(i32) <= propertyData_.size()) {
        memcpy(propertyData_.data() + it->second, &value, sizeof(i32));
        dirty_ = true;
    }
}

void MaterialInstance::setMatrix(const std::string& name, const Mat4& value) {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(Mat4) <= propertyData_.size()) {
        memcpy(propertyData_.data() + it->second, &value, sizeof(Mat4));
        dirty_ = true;
    }
}

void MaterialInstance::setTexture(const std::string& name, TextureHandle texture) {
    auto def = template_->getPropertyDef(name);
    if (def) {
        u32 binding = def->binding;
        if (binding < textures_.size()) {
            textures_[binding] = texture;
            dirty_ = true;
        }
    }
}

f32 MaterialInstance::getFloat(const std::string& name) const {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(f32) <= propertyData_.size()) {
        return *reinterpret_cast<const f32*>(propertyData_.data() + it->second);
    }
    return 0.0f;
}

Vec2 MaterialInstance::getFloat2(const std::string& name) const {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(Vec2) <= propertyData_.size()) {
        return *reinterpret_cast<const Vec2*>(propertyData_.data() + it->second);
    }
    return Vec2(0.0f);
}

Vec3 MaterialInstance::getFloat3(const std::string& name) const {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(Vec3) <= propertyData_.size()) {
        return *reinterpret_cast<const Vec3*>(propertyData_.data() + it->second);
    }
    return Vec3(0.0f);
}

Vec4 MaterialInstance::getFloat4(const std::string& name) const {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(Vec4) <= propertyData_.size()) {
        return *reinterpret_cast<const Vec4*>(propertyData_.data() + it->second);
    }
    return Vec4(0.0f);
}

i32 MaterialInstance::getInt(const std::string& name) const {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(i32) <= propertyData_.size()) {
        return *reinterpret_cast<const i32*>(propertyData_.data() + it->second);
    }
    return 0;
}

Mat4 MaterialInstance::getMatrix(const std::string& name) const {
    auto it = propertyOffsets_.find(name);
    if (it != propertyOffsets_.end() && it->second + sizeof(Mat4) <= propertyData_.size()) {
        return *reinterpret_cast<const Mat4*>(propertyData_.data() + it->second);
    }
    return Mat4::identity();
}

TextureHandle MaterialInstance::getTexture(const std::string& name) const {
    auto def = template_->getPropertyDef(name);
    if (def && def->binding < textures_.size()) {
        return textures_[def->binding];
    }
    return {};
}

void MaterialInstance::updateGPUData() {
    if (!dirty_) return;
    
    // 更新 GPU 缓冲
    // backend_->updateBuffer(propertyBuffer_, propertyData_.data(), propertyData_.size());
    
    dirty_ = false;
}

void MaterialInstance::bind(VkCommandBuffer cmd) {
    updateGPUData();
    
    // 绑定管线
    // backend_->setPipeline(template_->getPipeline());
    
    // 绑定纹理
    // ...
}

// ============================================================================
// PBRMaterial Implementation
// ============================================================================

PBRMaterial::PBRMaterial() = default;

PBRMaterial::~PBRMaterial() {
    shutdown();
}

bool PBRMaterial::initialize(VulkanBackend* backend) {
    backend_ = backend;
    instance_ = std::make_unique<MaterialInstance>();
    return true;
}

void PBRMaterial::shutdown() {
    instance_.reset();
    backend_ = nullptr;
}

void PBRMaterial::setAlbedoColor(const Vec4& color) {
    properties_.albedoColor = color;
    if (instance_) instance_->setFloat4("albedoColor", color);
}

void PBRMaterial::setEmissiveColor(const Vec4& color) {
    properties_.emissiveColor = color;
    if (instance_) instance_->setFloat4("emissiveColor", color);
}

void PBRMaterial::setMetallic(f32 value) {
    properties_.metallic = value;
    if (instance_) instance_->setFloat("metallic", value);
}

void PBRMaterial::setRoughness(f32 value) {
    properties_.roughness = value;
    if (instance_) instance_->setFloat("roughness", value);
}

void PBRMaterial::setAO(f32 value) {
    properties_.ao = value;
    if (instance_) instance_->setFloat("ao", value);
}

void PBRMaterial::setOpacity(f32 value) {
    properties_.opacity = value;
    if (instance_) instance_->setFloat("opacity", value);
}

void PBRMaterial::setAlbedoMap(TextureHandle texture) {
    properties_.albedoMap = texture;
    if (instance_) instance_->setTexture("albedoMap", texture);
}

void PBRMaterial::setNormalMap(TextureHandle texture) {
    properties_.normalMap = texture;
    if (instance_) instance_->setTexture("normalMap", texture);
}

void PBRMaterial::setMetallicRoughnessMap(TextureHandle texture) {
    properties_.metallicRoughnessMap = texture;
    if (instance_) instance_->setTexture("metallicRoughnessMap", texture);
}

void PBRMaterial::setAOMap(TextureHandle texture) {
    properties_.aoMap = texture;
    if (instance_) instance_->setTexture("aoMap", texture);
}

void PBRMaterial::setEmissiveMap(TextureHandle texture) {
    properties_.emissiveMap = texture;
    if (instance_) instance_->setTexture("emissiveMap", texture);
}

// ============================================================================
// MaterialSorter Implementation
// ============================================================================

u64 MaterialSorter::generateKey(MaterialInstance* material, const Vec3& cameraPosition) {
    if (!material) return UINT64_MAX;
    
    // 生成排序键:
    // 位 63-56: 透明度 (不透明在前)
    // 位 55-48: 着色器 ID
    // 位 47-32: 材质 ID
    // 位 31-0:  深度
    
    u64 key = 0;
    
    // 不透明物体在前
    // key |= (透明 ? 1 : 0) << 56;
    
    // 着色器 ID
    // key |= (shaderId & 0xFF) << 48;
    
    // 材质 ID
    // key |= (materialId & 0xFFFF) << 32;
    
    // 深度
    // key |= depth & 0xFFFFFFFF;
    
    return key;
}

void MaterialSorter::sort(std::vector<MaterialSortKey>& keys) {
    std::sort(keys.begin(), keys.end());
}

void MaterialSorter::sortAndDeduplicate(std::vector<MaterialSortKey>& keys) {
    sort(keys);
    
    // 去重 (相同材质)
    auto it = std::unique(keys.begin(), keys.end(),
        [](const MaterialSortKey& a, const MaterialSortKey& b) {
            return a.material == b.material;
        });
    keys.erase(it, keys.end());
}

// ============================================================================
// MaterialCache Implementation
// ============================================================================

MaterialCache::MaterialCache() = default;

MaterialCache::~MaterialCache() {
    shutdown();
}

bool MaterialCache::initialize(VulkanBackend* backend) {
    backend_ = backend;
    return true;
}

void MaterialCache::shutdown() {
    clear();
    backend_ = nullptr;
}

MaterialTemplate* MaterialCache::createTemplate(const std::string& name) {
    auto templ = std::make_unique<MaterialTemplate>();
    if (!templ->initialize(backend_, name)) {
        return nullptr;
    }
    
    auto ptr = templ.get();
    templates_[name] = std::move(templ);
    return ptr;
}

MaterialTemplate* MaterialCache::getTemplate(const std::string& name) {
    auto it = templates_.find(name);
    return it != templates_.end() ? it->second.get() : nullptr;
}

MaterialInstance* MaterialCache::createInstance(const std::string& templateName) {
    auto templ = getTemplate(templateName);
    if (!templ) return nullptr;
    return createInstance(templ);
}

MaterialInstance* MaterialCache::createInstance(MaterialTemplate* templ) {
    auto instance = std::make_unique<MaterialInstance>();
    if (!instance->initialize(templ)) {
        return nullptr;
    }
    
    auto ptr = instance.get();
    instances_.push_back(std::move(instance));
    return ptr;
}

PBRMaterial* MaterialCache::createPBRMaterial() {
    auto material = std::make_unique<PBRMaterial>();
    if (!material->initialize(backend_)) {
        return nullptr;
    }
    
    auto ptr = material.get();
    pbrMaterials_.push_back(std::move(material));
    return ptr;
}

void MaterialCache::releaseInstance(MaterialInstance* instance) {
    auto it = std::find_if(instances_.begin(), instances_.end(),
        [instance](const auto& ptr) { return ptr.get() == instance; });
    
    if (it != instances_.end()) {
        instances_.erase(it);
    }
}

void MaterialCache::releaseTemplate(MaterialTemplate* templ) {
    for (auto it = templates_.begin(); it != templates_.end(); ++it) {
        if (it->second.get() == templ) {
            templates_.erase(it);
            break;
        }
    }
}

void MaterialCache::clear() {
    instances_.clear();
    pbrMaterials_.clear();
    templates_.clear();
}

u32 MaterialCache::getTemplateCount() const {
    return static_cast<u32>(templates_.size());
}

u32 MaterialCache::getInstanceCount() const {
    return static_cast<u32>(instances_.size());
}

// ============================================================================
// BuiltinMaterials Implementation
// ============================================================================

BuiltinMaterials::BuiltinMaterials() = default;

BuiltinMaterials::~BuiltinMaterials() {
    shutdown();
}

bool BuiltinMaterials::initialize(VulkanBackend* backend) {
    backend_ = backend;
    return createBuiltinTemplates();
}

void BuiltinMaterials::shutdown() {
    templates_.clear();
    backend_ = nullptr;
}

MaterialTemplate* BuiltinMaterials::getMaterial(BuiltinMaterial type) {
    auto it = templates_.find(type);
    return it != templates_.end() ? it->second.get() : nullptr;
}

MaterialInstance* BuiltinMaterials::createInstance(BuiltinMaterial type) {
    auto templ = getMaterial(type);
    if (!templ) return nullptr;
    
    auto instance = std::make_unique<MaterialInstance>();
    if (!instance->initialize(templ)) {
        return nullptr;
    }
    
    // 内置材质实例不需要缓存管理
    return instance.release();
}

bool BuiltinMaterials::createBuiltinTemplates() {
    // Create PBR template
    {
        auto templ = std::make_unique<MaterialTemplate>();
        templ->initialize(backend_, "PBR");
        
        MaterialPropertyValue albedoVal;
        albedoVal.type = MaterialPropertyType::Float4;
        albedoVal.float4Value = Vec4(1.0f);
        templ->defineProperty("albedoColor", MaterialPropertyType::Float4, albedoVal);
        
        MaterialPropertyValue emissiveVal;
        emissiveVal.type = MaterialPropertyType::Float4;
        emissiveVal.float4Value = Vec4(0.0f);
        templ->defineProperty("emissiveColor", MaterialPropertyType::Float4, emissiveVal);
        
        MaterialPropertyValue metallicVal;
        metallicVal.type = MaterialPropertyType::Float;
        metallicVal.floatValue = 0.5f;
        templ->defineProperty("metallic", MaterialPropertyType::Float, metallicVal);
        
        MaterialPropertyValue roughnessVal;
        roughnessVal.type = MaterialPropertyType::Float;
        roughnessVal.floatValue = 0.5f;
        templ->defineProperty("roughness", MaterialPropertyType::Float, roughnessVal);
        
        MaterialPropertyValue aoVal;
        aoVal.type = MaterialPropertyType::Float;
        aoVal.floatValue = 1.0f;
        templ->defineProperty("ao", MaterialPropertyType::Float, aoVal);
        
        templ->defineTexture("albedoMap", 0);
        templ->defineTexture("normalMap", 1);
        templ->defineTexture("metallicRoughnessMap", 2);
        templ->defineTexture("aoMap", 3);
        templ->defineTexture("emissiveMap", 4);
        
        templates_[BuiltinMaterial::PBR] = std::move(templ);
    }
    
    // Create UI template
    {
        auto templ = std::make_unique<MaterialTemplate>();
        templ->initialize(backend_, "UI");
        
        MaterialPropertyValue colorVal;
        colorVal.type = MaterialPropertyType::Float4;
        colorVal.float4Value = Vec4(1.0f);
        templ->defineProperty("color", MaterialPropertyType::Float4, colorVal);
        templ->defineTexture("texture", 0);
        
        templates_[BuiltinMaterial::UI] = std::move(templ);
    }
    
    // Create RoundedRect template
    {
        auto templ = std::make_unique<MaterialTemplate>();
        templ->initialize(backend_, "RoundedRect");
        
        MaterialPropertyValue fillColor;
        fillColor.type = MaterialPropertyType::Float4;
        fillColor.float4Value = Vec4(1.0f);
        templ->defineProperty("fillColor", MaterialPropertyType::Float4, fillColor);
        
        MaterialPropertyValue borderColor;
        borderColor.type = MaterialPropertyType::Float4;
        borderColor.float4Value = Vec4(0.5f);
        templ->defineProperty("borderColor", MaterialPropertyType::Float4, borderColor);
        
        MaterialPropertyValue radiusVal;
        radiusVal.type = MaterialPropertyType::Float;
        radiusVal.floatValue = 16.0f;
        templ->defineProperty("radius", MaterialPropertyType::Float, radiusVal);
        
        MaterialPropertyValue borderVal;
        borderVal.type = MaterialPropertyType::Float;
        borderVal.floatValue = 2.0f;
        templ->defineProperty("borderWidth", MaterialPropertyType::Float, borderVal);
        
        templates_[BuiltinMaterial::RoundedRect] = std::move(templ);
    }
    
    // Create Particle template
    {
        auto templ = std::make_unique<MaterialTemplate>();
        templ->initialize(backend_, "Particle");
        
        MaterialPropertyValue particleColor;
        particleColor.type = MaterialPropertyType::Float4;
        particleColor.float4Value = Vec4(1.0f);
        templ->defineProperty("color", MaterialPropertyType::Float4, particleColor);
        templ->defineTexture("texture", 0);
        
        templates_[BuiltinMaterial::Particle] = std::move(templ);
    }
    
    return true;
}

} // namespace Nova
