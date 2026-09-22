// Verify that consumers can link the compiled library without implementation macros.
#include <clay.h>
#include <clay-widgets/widgets.h>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
Clay_Dimensions measure(Clay_StringSlice text, Clay_TextElementConfig* config, void*) {
    return {static_cast<float>(text.length) * config->fontSize * 0.5f,
        static_cast<float>(config->fontSize)};
}
}

int main() {
    const auto size = Clay_MinMemorySize();
    std::unique_ptr<void, decltype(&std::free)> memory(std::malloc(size), &std::free);
    if (!memory) return 1;
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(size, memory.get()), {320, 200}, {});
    Clay_SetMeasureTextFunction(measure, nullptr);
    // The context contains large fixed-size state pools; keep it off the stack.
    auto ui = std::make_unique<ClayWidgets_Context>();
    ClayWidgets_Init(ui.get(), ClayWidgets_DefaultTheme());
    ClayWidgets_SetMeasureTextFunction(ui.get(), measure, nullptr);
    ui->animationsEnabled = false;
    ClayWidgets_BeginFrame(ui.get(), {}, {320, 200}, false);
    ClayWidgets_Label(ui.get(), CLAY_STRING("Usage remaining"));
    const auto commands = ClayWidgets_EndFrame(ui.get());
    for (int i = 0; i < commands.length; ++i) {
        if (commands.internalArray[i].commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            std::cout << "PASS: compiled Clay/widgets library linked and emitted text.\n";
            return 0;
        }
    }
    std::cerr << "FAIL: compiled UI library did not emit a text command.\n";
    return 1;
}
