/**
 * Nova Renderer - Example Application
 * 基础示例
 */

#include <Nova/Renderer.h>
#include <Nova/Window.h>
#include <Nova/Input.h>
#include <iostream>

using namespace Nova;

// 示例状态
struct AppState {
    Vec2 mousePos;
    Color bgColor = Color::fromHex(0x1E1E2EFF);
    Color accentColor = Color::fromHex(0x89B4FAFF);
    
    f32 animationTime = 0.0f;
    bool showDemo = true;
};

void onRender(const FrameContext& ctx) {
    auto* state = (AppState*)GetRenderer()->getWindow()->userData;
    auto* input = GetRenderer()->getInput();
    auto* backend = GetRenderer()->getBackend();
    
    // 更新动画时间
    state->animationTime += ctx.deltaTime;
    
    // 更新鼠标位置
    state->mousePos = input->getMousePosition();
    
    // 清屏
    GetRenderer()->clear(state->bgColor);
    
    // 绘制一些示例元素
    
    // 动画矩形
    f32 pulse = 0.5f + 0.5f * sinf(state->animationTime * 2.0f);
    Color animColor = Color::lerp(state->accentColor, Color::white(), pulse * 0.3f);
    
    GetRenderer()->drawRoundRect(
        {ctx.screenWidth / 2 - 150, ctx.screenHeight / 2 - 100, 300, 200},
        20.0f,
        animColor
    );
    
    // 鼠标跟随圆
    GetRenderer()->drawCircle(state->mousePos, 20.0f, state->accentColor);
    
    // 文本
    GetRenderer()->drawText("Nova Renderer", {ctx.screenWidth / 2 - 80, 50}, Color::white(), 24.0f);
    
    // FPS 显示
    char fpsText[64];
    snprintf(fpsText, sizeof(fpsText), "FPS: %.1f", 1.0f / ctx.deltaTime);
    GetRenderer()->drawText(fpsText, {10, 10}, Color::white(), 16.0f);
}

void onResize(u32 width, u32 height) {
    std::cout << "Window resized: " << width << "x" << height << std::endl;
}

int main() {
    std::cout << "Nova Renderer Example" << std::endl;
    std::cout << "=====================" << std::endl;
    
    // 配置渲染器
    RendererConfig config;
    config.window.title = "Nova Renderer - Example";
    config.window.width = 1280;
    config.window.height = 720;
    config.enableValidation = true;
    config.enableDebugUtils = true;
    config.vSync = true;
    
    // 创建渲染器
    Renderer renderer;
    auto result = renderer.initialize(config);
    
    if (!result.success) {
        std::cerr << "Failed to initialize renderer: " << result.error << std::endl;
        return 1;
    }
    
    std::cout << "Renderer initialized successfully!" << std::endl;
    std::cout << "GPU: " << renderer.getBackend()->getDeviceProperties().deviceName << std::endl;
    
    // 创建应用状态
    AppState state;
    renderer.getWindow()->userData = &state;
    
    // 设置回调
    renderer.setRenderCallback(onRender);
    renderer.setResizeCallback(onResize);
    
    // 设置输入回调
    renderer.getInput()->setKeyCallback([](const KeyEvent& e) {
        if (e.key == KeyCode::Escape && e.action == InputAction::Press) {
            GetRenderer()->stop();
        }
        if (e.key == KeyCode::F11 && e.action == InputAction::Press) {
            static bool fullscreen = false;
            fullscreen = !fullscreen;
            GetRenderer()->getWindow()->setFullscreen(fullscreen);
        }
    });
    
    renderer.getInput()->setMouseCallback([](const MouseEvent& e) {
        if (e.button == MouseButton::Left && e.action == InputAction::Press) {
            std::cout << "Mouse clicked at: " << e.position.x << ", " << e.position.y << std::endl;
        }
    });
    
    std::cout << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  ESC     - Exit" << std::endl;
    std::cout << "  F11     - Toggle fullscreen" << std::endl;
    std::cout << std::endl;
    
    // 运行主循环
    renderer.run();
    
    std::cout << "Shutting down..." << std::endl;
    
    return 0;
}
