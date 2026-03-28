# UI Framework Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the UI framework (UIManager, UIScreen, UIView, UIModal, UIWidget base classes), wire UIManager into main.cpp, and migrate MainMenuScreen as proof of concept. Legacy screens continue working via a LegacyScreen adapter.

**Architecture:** UIManager owns a screen stack and modal stack. Screens are UIScreen subclasses with lifecycle hooks. main.cpp's switch statement is replaced by UIManager::draw(). Legacy draw_*() functions are wrapped in LegacyScreen adapters so migration is incremental.

**Tech Stack:** C++20, SDL2, ImGui, CMake

**Spec:** `neuroflyer/docs/superpowers/specs/2026-03-26-ui-architecture-design.md`

---

### Task 1: UIScreen and UIView base class headers

**Files:**
- Create: `neuroflyer/include/neuroflyer/ui/ui_screen.h`
- Create: `neuroflyer/include/neuroflyer/ui/ui_view.h`

- [ ] **Step 1: Create UIScreen base class header**

Create `neuroflyer/include/neuroflyer/ui/ui_screen.h`:

```cpp
#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

namespace neuroflyer {

class UIManager;

class UIScreen {
public:
    virtual ~UIScreen() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void on_resize(int w, int h) { (void)w; (void)h; }
    virtual void on_draw(AppState& state, Renderer& renderer, UIManager& ui) = 0;
    [[nodiscard]] virtual const char* name() const = 0;
};

} // namespace neuroflyer
```

Note: `on_draw` takes `UIManager&` so screens can call `ui.push_screen()`, `ui.pop_screen()`, etc. from within their draw function.

- [ ] **Step 2: Create UIView base class header**

Create `neuroflyer/include/neuroflyer/ui/ui_view.h`:

```cpp
#pragma once

#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

namespace neuroflyer {

class UIManager;

class UIView {
public:
    virtual ~UIView() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void on_resize(int w, int h) { (void)w; (void)h; }
    virtual void on_draw(AppState& state, Renderer& renderer, UIManager& ui) = 0;

    void set_bounds(float x, float y, float w, float h) {
        x_ = x; y_ = y; w_ = w; h_ = h;
    }
    [[nodiscard]] float x() const { return x_; }
    [[nodiscard]] float y() const { return y_; }
    [[nodiscard]] float w() const { return w_; }
    [[nodiscard]] float h() const { return h_; }

protected:
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};

} // namespace neuroflyer
```

- [ ] **Step 3: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`
Expected: Clean build (headers not yet included anywhere, but syntax is checked if any file transitively includes them).

- [ ] **Step 4: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/include/neuroflyer/ui/ui_screen.h \
       neuroflyer/include/neuroflyer/ui/ui_view.h && \
git commit -m "feat(neuroflyer): UIScreen and UIView base class headers"
```

---

### Task 2: UIModal base class header

**Files:**
- Create: `neuroflyer/include/neuroflyer/ui/ui_modal.h`

- [ ] **Step 1: Create UIModal base class header**

Create `neuroflyer/include/neuroflyer/ui/ui_modal.h`:

```cpp
#pragma once

#include <neuroflyer/app_state.h>

namespace neuroflyer {

class UIManager;

class UIModal {
public:
    virtual ~UIModal() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void on_draw(AppState& state, UIManager& ui) = 0;
    [[nodiscard]] virtual const char* name() const = 0;
    [[nodiscard]] virtual bool blocks_input() const { return true; }
};

} // namespace neuroflyer
```

- [ ] **Step 2: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/include/neuroflyer/ui/ui_modal.h && \
git commit -m "feat(neuroflyer): UIModal base class header"
```

---

### Task 3: UIManager header and implementation

**Files:**
- Create: `neuroflyer/include/neuroflyer/ui/ui_manager.h`
- Create: `neuroflyer/src/ui/framework/ui_manager.cpp`
- Modify: `neuroflyer/CMakeLists.txt`

- [ ] **Step 1: Create UIManager header**

Create `neuroflyer/include/neuroflyer/ui/ui_manager.h`:

```cpp
#pragma once

#include <neuroflyer/ui/ui_screen.h>
#include <neuroflyer/ui/ui_modal.h>
#include <neuroflyer/app_state.h>
#include <neuroflyer/renderer.h>

#include <memory>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;

namespace neuroflyer {

class UIManager {
public:
    UIManager(SDL_Window* window, SDL_Renderer* sdl_renderer,
              int width, int height);

    // Screen stack
    void push_screen(std::unique_ptr<UIScreen> screen);
    void pop_screen();
    void replace_screen(std::unique_ptr<UIScreen> screen);
    [[nodiscard]] UIScreen* active_screen() const;
    [[nodiscard]] bool has_screens() const;

    // Modal stack
    void push_modal(std::unique_ptr<UIModal> modal);
    void pop_modal();
    [[nodiscard]] bool has_modal() const;

    // Resolution
    void set_resolution(int width, int height);
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }

    // Main loop — draws active screen + modal stack
    void draw(AppState& state, Renderer& renderer);

private:
    std::vector<std::unique_ptr<UIScreen>> screen_stack_;
    std::vector<std::unique_ptr<UIModal>> modal_stack_;
    int width_;
    int height_;
    SDL_Window* window_;
    SDL_Renderer* sdl_renderer_;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Create UIManager implementation**

Create `neuroflyer/src/ui/framework/ui_manager.cpp`:

```cpp
#include <neuroflyer/ui/ui_manager.h>

namespace neuroflyer {

UIManager::UIManager(SDL_Window* window, SDL_Renderer* sdl_renderer,
                     int width, int height)
    : width_(width), height_(height), window_(window),
      sdl_renderer_(sdl_renderer) {}

// --- Screen stack ---

void UIManager::push_screen(std::unique_ptr<UIScreen> screen) {
    if (!screen_stack_.empty()) {
        screen_stack_.back()->on_exit();
    }
    screen->on_resize(width_, height_);
    screen->on_enter();
    screen_stack_.push_back(std::move(screen));
}

void UIManager::pop_screen() {
    if (screen_stack_.size() <= 1) return;  // can't pop the last screen
    screen_stack_.back()->on_exit();
    screen_stack_.pop_back();
    screen_stack_.back()->on_enter();
}

void UIManager::replace_screen(std::unique_ptr<UIScreen> screen) {
    // Exit all screens in stack
    for (auto it = screen_stack_.rbegin(); it != screen_stack_.rend(); ++it) {
        (*it)->on_exit();
    }
    screen_stack_.clear();
    screen->on_resize(width_, height_);
    screen->on_enter();
    screen_stack_.push_back(std::move(screen));
}

UIScreen* UIManager::active_screen() const {
    if (screen_stack_.empty()) return nullptr;
    return screen_stack_.back().get();
}

bool UIManager::has_screens() const {
    return !screen_stack_.empty();
}

// --- Modal stack ---

void UIManager::push_modal(std::unique_ptr<UIModal> modal) {
    modal->on_enter();
    modal_stack_.push_back(std::move(modal));
}

void UIManager::pop_modal() {
    if (modal_stack_.empty()) return;
    modal_stack_.back()->on_exit();
    modal_stack_.pop_back();
}

bool UIManager::has_modal() const {
    return !modal_stack_.empty();
}

// --- Resolution ---

void UIManager::set_resolution(int width, int height) {
    width_ = width;
    height_ = height;
    for (auto& screen : screen_stack_) {
        screen->on_resize(width, height);
    }
}

// --- Draw ---

void UIManager::draw(AppState& state, Renderer& renderer) {
    // Draw the active screen
    if (!screen_stack_.empty()) {
        screen_stack_.back()->on_draw(state, renderer, *this);
    }

    // Draw modals bottom-to-top
    for (auto& modal : modal_stack_) {
        modal->on_draw(state, *this);
    }
}

} // namespace neuroflyer
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `neuroflyer/CMakeLists.txt`, add after the `src/ui/asset_loader.cpp` line:

```cmake
    src/ui/framework/ui_manager.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`
Expected: Clean build.

- [ ] **Step 5: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/include/neuroflyer/ui/ui_manager.h \
       neuroflyer/src/ui/framework/ui_manager.cpp \
       neuroflyer/CMakeLists.txt && \
git commit -m "feat(neuroflyer): UIManager with screen/modal stacks and resolution"
```

---

### Task 4: UIWidget namespace

**Files:**
- Create: `neuroflyer/include/neuroflyer/ui/ui_widget.h`
- Create: `neuroflyer/src/ui/framework/ui_widget.cpp`
- Modify: `neuroflyer/CMakeLists.txt`

- [ ] **Step 1: Create UIWidget header**

Create `neuroflyer/include/neuroflyer/ui/ui_widget.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace neuroflyer::ui {

// Styled button variants
enum class ButtonStyle { Primary, Secondary, Danger };

bool button(const char* label, ButtonStyle style = ButtonStyle::Primary,
            float width = 0.0f);

bool slider_float(const char* label, float* value, float min, float max);
bool input_int(const char* label, int* value, int min, int max);
bool input_text(const char* label, char* buf, std::size_t buf_size,
                const char* hint = nullptr);
bool checkbox(const char* label, bool* value);
void section_header(const char* label);

} // namespace neuroflyer::ui
```

- [ ] **Step 2: Create UIWidget implementation**

Create `neuroflyer/src/ui/framework/ui_widget.cpp`:

```cpp
#include <neuroflyer/ui/ui_widget.h>

#include <imgui.h>

#include <algorithm>

namespace neuroflyer::ui {

bool button(const char* label, ButtonStyle style, float width) {
    ImVec4 color;
    ImVec4 hovered;
    ImVec4 active;

    switch (style) {
    case ButtonStyle::Primary:
        color   = ImVec4(0.2f, 0.5f, 0.8f, 1.0f);
        hovered = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
        active  = ImVec4(0.15f, 0.4f, 0.7f, 1.0f);
        break;
    case ButtonStyle::Secondary:
        color   = ImVec4(0.3f, 0.3f, 0.35f, 1.0f);
        hovered = ImVec4(0.4f, 0.4f, 0.45f, 1.0f);
        active  = ImVec4(0.25f, 0.25f, 0.3f, 1.0f);
        break;
    case ButtonStyle::Danger:
        color   = ImVec4(0.7f, 0.15f, 0.15f, 1.0f);
        hovered = ImVec4(0.85f, 0.2f, 0.2f, 1.0f);
        active  = ImVec4(0.6f, 0.1f, 0.1f, 1.0f);
        break;
    }

    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);

    ImVec2 size = (width > 0.0f) ? ImVec2(width, 0) : ImVec2(0, 0);
    bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleColor(3);
    return clicked;
}

bool slider_float(const char* label, float* value, float min, float max) {
    ImGui::PushItemWidth(-1);
    ImGui::Text("%s", label);
    ImGui::SameLine(150.0f);
    bool changed = ImGui::SliderFloat(
        (std::string("##") + label).c_str(), value, min, max);
    ImGui::PopItemWidth();
    return changed;
}

bool input_int(const char* label, int* value, int min, int max) {
    ImGui::Text("%s", label);
    ImGui::SameLine(150.0f);
    ImGui::PushItemWidth(-1);
    bool changed = ImGui::InputInt(
        (std::string("##") + label).c_str(), value);
    ImGui::PopItemWidth();
    *value = std::clamp(*value, min, max);
    return changed;
}

bool input_text(const char* label, char* buf, std::size_t buf_size,
                const char* hint) {
    ImGui::Text("%s", label);
    ImGui::SameLine(150.0f);
    ImGui::PushItemWidth(-1);
    bool changed;
    if (hint) {
        changed = ImGui::InputTextWithHint(
            (std::string("##") + label).c_str(), hint, buf, buf_size);
    } else {
        changed = ImGui::InputText(
            (std::string("##") + label).c_str(), buf, buf_size);
    }
    ImGui::PopItemWidth();
    return changed;
}

bool checkbox(const char* label, bool* value) {
    return ImGui::Checkbox(label, value);
}

void section_header(const char* label) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "%s", label);
    ImGui::Separator();
    ImGui::Spacing();
}

} // namespace neuroflyer::ui
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `neuroflyer/CMakeLists.txt`, add after the `ui_manager.cpp` line:

```cmake
    src/ui/framework/ui_widget.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`
Expected: Clean build.

- [ ] **Step 5: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/include/neuroflyer/ui/ui_widget.h \
       neuroflyer/src/ui/framework/ui_widget.cpp \
       neuroflyer/CMakeLists.txt && \
git commit -m "feat(neuroflyer): UIWidget styled control helpers"
```

---

### Task 5: LegacyScreen adapter

**Files:**
- Create: `neuroflyer/include/neuroflyer/ui/legacy_screen.h`

- [ ] **Step 1: Create LegacyScreen adapter**

This adapter wraps existing `draw_*()` free functions so they work with UIManager during the migration period. Create `neuroflyer/include/neuroflyer/ui/legacy_screen.h`:

```cpp
#pragma once

#include <neuroflyer/ui/ui_screen.h>

#include <functional>
#include <string>

namespace neuroflyer {

/// Adapter that wraps a legacy draw_*() free function as a UIScreen.
/// Used during incremental migration — legacy screens work alongside
/// new UIScreen subclasses without changes.
class LegacyScreen : public UIScreen {
public:
    using DrawFn = std::function<void(AppState&, Renderer&)>;

    LegacyScreen(std::string screen_name, DrawFn draw_fn)
        : name_(std::move(screen_name)), draw_fn_(std::move(draw_fn)) {}

    void on_draw(AppState& state, Renderer& renderer,
                 UIManager& /*ui*/) override {
        draw_fn_(state, renderer);
    }

    [[nodiscard]] const char* name() const override {
        return name_.c_str();
    }

private:
    std::string name_;
    DrawFn draw_fn_;
};

} // namespace neuroflyer
```

- [ ] **Step 2: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/include/neuroflyer/ui/legacy_screen.h && \
git commit -m "feat(neuroflyer): LegacyScreen adapter for incremental migration"
```

---

### Task 6: MainMenuScreen — first real UIScreen

**Files:**
- Create: `neuroflyer/include/neuroflyer/ui/screens/main_menu_screen.h`
- Create: `neuroflyer/src/ui/screens/menu/main_menu_screen.cpp`
- Modify: `neuroflyer/CMakeLists.txt`

- [ ] **Step 1: Create MainMenuScreen header**

Create `neuroflyer/include/neuroflyer/ui/screens/main_menu_screen.h`:

```cpp
#pragma once

#include <neuroflyer/ui/ui_screen.h>

namespace neuroflyer {

class MainMenuScreen : public UIScreen {
public:
    void on_draw(AppState& state, Renderer& renderer,
                 UIManager& ui) override;
    [[nodiscard]] const char* name() const override { return "MainMenu"; }
};

} // namespace neuroflyer
```

- [ ] **Step 2: Create MainMenuScreen implementation**

Create `neuroflyer/src/ui/screens/menu/main_menu_screen.cpp`:

```cpp
#include <neuroflyer/ui/screens/main_menu_screen.h>
#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/legacy_screen.h>

#include <neuroflyer/screens/hangar/hangar.h>
#include <neuroflyer/screens/game/fly_session.h>

#include <imgui.h>

namespace neuroflyer {

void MainMenuScreen::on_draw(AppState& state, Renderer& renderer,
                              UIManager& ui) {
    renderer.render_menu_background();

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float sw = display.x;
    const float sh = display.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.3f);

    ImGui::Begin("##MainMenu", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar);

    // Title
    ImGui::Dummy(ImVec2(0, sh * 0.2f));

    {
        ImGui::PushFont(nullptr);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.9f, 0.5f, 1.0f));
        const char* title = "NEUROFLYER";
        float title_w = ImGui::CalcTextSize(title).x;
        ImGui::SetCursorPosX((sw - title_w) * 0.5f);
        ImGui::Text("%s", title);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    ImGui::Dummy(ImVec2(0, 10));

    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.6f, 1.0f));
        const char* subtitle = "Neuroevolution Arcade";
        float sub_w = ImGui::CalcTextSize(subtitle).x;
        ImGui::SetCursorPosX((sw - sub_w) * 0.5f);
        ImGui::Text("%s", subtitle);
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, sh * 0.1f));

    // Menu buttons -- centered
    constexpr float BTN_W = 250.0f;
    constexpr float BTN_H = 50.0f;
    constexpr float BTN_GAP = 12.0f;
    float btn_x = (sw - BTN_W) * 0.5f;

    ImGui::SetCursorPosX(btn_x);
    if (ImGui::Button("Fly", ImVec2(BTN_W, BTN_H))) {
        // Wrap legacy fly session as a LegacyScreen
        ui.push_screen(std::make_unique<LegacyScreen>("Flying",
            [](AppState& s, Renderer& r) { draw_fly_session(s, r); }));
    }

    ImGui::Dummy(ImVec2(0, BTN_GAP));
    ImGui::SetCursorPosX(btn_x);
    if (ImGui::Button("Hangar", ImVec2(BTN_W, BTN_H))) {
        // Wrap legacy hangar as a LegacyScreen
        ui.push_screen(std::make_unique<LegacyScreen>("Hangar",
            [](AppState& s, Renderer& r) { draw_hangar(s, r); }));
    }

    ImGui::Dummy(ImVec2(0, BTN_GAP));
    ImGui::SetCursorPosX(btn_x);
    if (ImGui::Button("Quit", ImVec2(BTN_W, BTN_H))) {
        state.quit_requested = true;
    }

    ImGui::End();
}

} // namespace neuroflyer
```

- [ ] **Step 3: Add to CMakeLists.txt**

In `neuroflyer/CMakeLists.txt`, add after the `ui_widget.cpp` line:

```cmake
    src/ui/screens/menu/main_menu_screen.cpp
```

- [ ] **Step 4: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`
Expected: Clean build.

- [ ] **Step 5: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/include/neuroflyer/ui/screens/main_menu_screen.h \
       neuroflyer/src/ui/screens/menu/main_menu_screen.cpp \
       neuroflyer/CMakeLists.txt && \
git commit -m "feat(neuroflyer): MainMenuScreen — first UIScreen subclass"
```

---

### Task 7: Wire UIManager into main.cpp

**Files:**
- Modify: `neuroflyer/src/ui/main.cpp`

- [ ] **Step 1: Replace the switch statement with UIManager**

Replace the contents of `neuroflyer/src/ui/main.cpp` with:

```cpp
#include <neuroflyer/app_state.h>
#include <neuroflyer/constants.h>
#include <neuroflyer/paths.h>
#include <neuroflyer/renderer.h>
#include <neuroflyer/config.h>
#include <neuroflyer/genome_manager.h>

#include <neuroflyer/ui/ui_manager.h>
#include <neuroflyer/ui/screens/main_menu_screen.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <random>

int main() {
    using neuroflyer::SCREEN_W;
    using neuroflyer::SCREEN_H;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    auto* window = SDL_CreateWindow("NeuroFlyer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    auto* sdl_renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    neuroflyer::Renderer renderer(sdl_renderer, window, SCREEN_W, SCREEN_H);

    // Load assets
    {
        auto assets = neuroflyer::asset_dir();
        renderer.load_asteroid_texture(assets + "/asteroid-1.png");
        renderer.load_coin_strip(assets + "/coin-strip.png", 16);
        renderer.load_star_atlas(assets + "/star-atlas.png", 42);
        renderer.load_menu_background(assets + "/pixel-hangar.png");
        for (int i = 0; i < 10; ++i) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s/ships/ship-%02d-strip.png", assets.c_str(), i);
            renderer.load_ship_strip(buf, 4);
        }
    }

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 3.0f;
    style.FramePadding = ImVec2(8.0f, 4.0f);
    ImGui_ImplSDL2_InitForSDLRenderer(window, sdl_renderer);
    ImGui_ImplSDLRenderer2_Init(sdl_renderer);

    // App state
    neuroflyer::AppState state;
    state.data_dir = neuroflyer::data_dir();
    state.asset_dir = neuroflyer::asset_dir();
    state.settings_path = state.data_dir + "/settings.json";
    std::filesystem::create_directories(state.data_dir + "/nets");
    std::filesystem::create_directories(state.data_dir + "/genomes");
    state.config = neuroflyer::GameConfig::load(state.settings_path);
    state.rng.seed(std::random_device{}());

    // Recover auto-saves
    (void)neuroflyer::recover_autosaves(state.data_dir + "/genomes");

    // UI Manager — push main menu as the root screen
    neuroflyer::UIManager ui(window, sdl_renderer, SCREEN_W, SCREEN_H);
    ui.push_screen(std::make_unique<neuroflyer::MainMenuScreen>());

    // Main loop
    while (!state.quit_requested) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) state.quit_requested = true;
        }

        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // UIManager handles screen + modal dispatch
        ui.draw(state, renderer);

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), sdl_renderer);

        // Execute deferred SDL draws (on top of ImGui)
        renderer.flush_deferred();

        SDL_RenderPresent(sdl_renderer);
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
```

Key changes from the current main.cpp:
- The `Screen` enum switch is gone — replaced by `ui.draw(state, renderer)`
- `UIManager ui` is created after ImGui init, pushed with `MainMenuScreen`
- All screen includes except main_menu_screen.h are removed from main.cpp
- The legacy screen includes move to `main_menu_screen.cpp` where the LegacyScreen adapters are created

- [ ] **Step 2: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`
Expected: Clean build.

- [ ] **Step 3: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/src/ui/main.cpp && \
git commit -m "feat(neuroflyer): wire UIManager into main loop, replacing switch dispatch"
```

---

### Task 8: Handle legacy screen navigation

**Problem:** Legacy screens still call `go_to_screen(state, Screen::X)` which updates `AppState::current`. But UIManager doesn't watch that field — it uses the screen stack. We need legacy screens to keep working during migration.

**Solution:** After `ui.draw()`, check if `state.current` changed (a legacy screen navigated). If so, sync the UIManager stack.

**Files:**
- Modify: `neuroflyer/src/ui/main.cpp`
- Modify: `neuroflyer/include/neuroflyer/ui/ui_manager.h`
- Modify: `neuroflyer/src/ui/framework/ui_manager.cpp`

- [ ] **Step 1: Add legacy navigation bridge to UIManager**

In `neuroflyer/include/neuroflyer/ui/ui_manager.h`, add to the public section:

```cpp
    /// Bridge for legacy go_to_screen() calls during migration.
    /// Call after draw() — checks if AppState::current changed and syncs the stack.
    void sync_legacy_navigation(AppState& state, Renderer& renderer);
```

Add a private member:

```cpp
    Screen last_known_screen_ = Screen::MainMenu;
```

This requires adding `#include <neuroflyer/app_state.h>` which is already there (for the Screen enum).

- [ ] **Step 2: Implement sync_legacy_navigation**

In `neuroflyer/src/ui/framework/ui_manager.cpp`, add:

```cpp
void UIManager::sync_legacy_navigation(AppState& state, Renderer& renderer) {
    if (state.current == last_known_screen_) return;

    // A legacy screen called go_to_screen() — sync the stack
    last_known_screen_ = state.current;

    // Pop back to root and push the appropriate legacy screen
    while (screen_stack_.size() > 1) {
        screen_stack_.back()->on_exit();
        screen_stack_.pop_back();
    }

    // Map Screen enum to legacy draw functions
    using S = Screen;
    std::unique_ptr<UIScreen> next;

    switch (state.current) {
    case S::MainMenu:
        // Pop back to MainMenuScreen (already root)
        return;
    case S::Hangar:
        next = std::make_unique<LegacyScreen>("Hangar",
            [](AppState& s, Renderer& r) { draw_hangar(s, r); });
        break;
    case S::CreateGenome:
        next = std::make_unique<LegacyScreen>("CreateGenome",
            [](AppState& s, Renderer& r) { draw_create_genome(s, r); });
        break;
    case S::VariantViewer:
        next = std::make_unique<LegacyScreen>("VariantViewer",
            [](AppState& s, Renderer& r) { draw_variant_viewer(s, r); });
        break;
    case S::LineageTree:
        next = std::make_unique<LegacyScreen>("LineageTree",
            [](AppState& s, Renderer& r) { draw_lineage_tree_screen(s, r); });
        break;
    case S::Flying:
        next = std::make_unique<LegacyScreen>("Flying",
            [](AppState& s, Renderer& r) { draw_fly_session(s, r); });
        break;
    case S::PauseConfig:
        next = std::make_unique<LegacyScreen>("PauseConfig",
            [](AppState& s, Renderer& r) {
                draw_pause_config(s, r, get_fly_session_state());
            });
        break;
    }

    if (next) {
        push_screen(std::move(next));
    }
}
```

Add the needed includes at the top of `ui_manager.cpp`:

```cpp
#include <neuroflyer/ui/legacy_screen.h>
#include <neuroflyer/screens/hangar/hangar.h>
#include <neuroflyer/screens/hangar/create_genome.h>
#include <neuroflyer/screens/hangar/variant_viewer.h>
#include <neuroflyer/screens/hangar/lineage_tree.h>
#include <neuroflyer/screens/game/fly_session.h>
#include <neuroflyer/screens/game/pause_config.h>
```

- [ ] **Step 3: Call the bridge in main.cpp**

In `neuroflyer/src/ui/main.cpp`, after the `ui.draw(state, renderer);` line, add:

```cpp
        ui.sync_legacy_navigation(state, renderer);
```

- [ ] **Step 4: Build and verify**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu)`
Expected: Clean build.

- [ ] **Step 5: Commit**

```bash
cd /Users/wldarden/learning/cPlusPlus && \
git add neuroflyer/include/neuroflyer/ui/ui_manager.h \
       neuroflyer/src/ui/framework/ui_manager.cpp \
       neuroflyer/src/ui/main.cpp && \
git commit -m "feat(neuroflyer): legacy navigation bridge for incremental migration"
```

---

### Task 9: Smoke test

- [ ] **Step 1: Build and launch**

Run: `cd /Users/wldarden/learning/cPlusPlus && cmake --build build --target neuroflyer -j$(sysctl -n hw.ncpu) && ./build/neuroflyer/neuroflyer`

- [ ] **Step 2: Verify main menu**

1. App opens to the main menu (title, Fly/Hangar/Quit buttons)
2. Background image renders

- [ ] **Step 3: Verify navigation to legacy screens**

1. Click "Hangar" — hangar screen appears, genome list works
2. Navigate into a genome's variant viewer — variants display
3. Click back to hangar — hangar state preserved
4. Press Escape to return to main menu

- [ ] **Step 4: Verify fly mode**

1. From main menu click "Fly" — fly session starts
2. Press Space to pause — pause config appears
3. Resume — returns to flight
4. Navigation between all screens works as before

- [ ] **Step 5: Verify Quit**

1. Click "Quit" — app exits cleanly

- [ ] **Step 6: Commit any fixes**

If issues found, fix and commit with descriptive messages.
