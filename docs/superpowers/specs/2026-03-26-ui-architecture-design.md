# UI Architecture Design

**Date:** 2026-03-26
**Scope:** NeuroFlyer — UI framework with screen stack, views, modals, and widgets

## Summary

Replace the current flat `draw_*()` free functions with a 4-layer UI framework: UIManager (orchestrator), UIScreen (top-level contexts), UIView (panels within screens), UIModal (overlays), and UIWidget (consistent styled controls). Migrated incrementally — one screen at a time, app runs at every step.

## Problem

The current UI has no shared structure:
- Every screen manually sets up ImGui windows with duplicated boilerplate
- State lives in static locals scattered across translation units — no lifecycle management
- Resolution is hardcoded, no dynamic resize support
- Moving panels between screens means rewriting them
- Adding a new screen means copying hundreds of lines of boilerplate
- Navigation is a flat enum switch with no stack — screens reachable from multiple locations have spaghetti "where do I go back to" logic

## Mental Model — 4 Layers

### Layer 1: Screen (Top-level app state)

Major game contexts: MainMenu, Hangar, Game, etc. Managed via a stack — push to go deeper, pop to go back. Each screen owns its state as member variables (not static locals).

### Layer 2: View (Panels within a screen)

Regions/tools inside a screen: genome list panel, preview panel, neural net editor, HUD. A screen composes views and controls their layout. Views know their bounds but don't decide them.

### Layer 3: Modal (Temporary overlays)

UI rendered on top of everything: confirm dialogs, input prompts, settings. Managed via a stack — modals can push other modals. Optionally block input to the screen below.

### Layer 4: Widget (Atomic controls)

Styled free functions in a `ui::` namespace: buttons, sliders, text inputs, checkboxes. Not classes — ImGui's immediate-mode pattern. Wrappers ensure consistent look across the app.

## UIManager

The central orchestrator. Owns the screen stack, modal stack, and resolution state.

```cpp
class UIManager {
public:
    UIManager(SDL_Window* window, SDL_Renderer* renderer, int width, int height);

    // Screen stack
    void push_screen(std::unique_ptr<UIScreen> screen);
    void pop_screen();
    void replace_screen(std::unique_ptr<UIScreen> screen);

    // Modal stack
    void push_modal(std::unique_ptr<UIModal> modal);
    void pop_modal();

    // Resolution
    void set_resolution(int width, int height);
    int width() const;
    int height() const;

    // Main loop — draws active screen + modal stack
    void draw(AppState& state, Renderer& renderer);

private:
    std::vector<std::unique_ptr<UIScreen>> screen_stack_;
    std::vector<std::unique_ptr<UIModal>> modal_stack_;
    int width_, height_;
    SDL_Window* window_;
    SDL_Renderer* sdl_renderer_;
};
```

**Behavior:**
- `push_screen()` calls `on_exit()` on current screen, `on_enter()` on the new one
- `pop_screen()` calls `on_exit()` on current, `on_enter()` on the one below (state preserved)
- `replace_screen()` clears the stack, pushes the new screen
- `draw()` calls `on_draw()` on the top screen, then draws all modals bottom-to-top
- `set_resolution()` calls `on_resize()` on all screens in the stack (so they're ready when returned to)
- `pop_screen()` with one screen on the stack is a no-op (can't pop the last screen — use `replace_screen()` to change root)

## UIScreen

Base class for all screens. Virtual methods for lifecycle and rendering.

```cpp
class UIScreen {
public:
    virtual ~UIScreen() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void on_resize(int w, int h) {}
    virtual void on_draw(AppState& state, Renderer& renderer) = 0;
    virtual const char* name() const = 0;
};
```

**State ownership:** All state that currently lives in static locals (`s_genomes`, `s_selected_idx`, etc.) moves to member variables on the screen subclass. Two instances of the same screen type have independent state.

**Concrete screens:** MainMenuScreen, HangarScreen, CreateGenomeScreen, VariantViewerScreen, LineageTreeScreen, FlySessionScreen, PauseConfigScreen, AnalysisScreen.

## UIView

Panels/regions within a screen. A screen owns its views and sets their bounds via layout logic.

```cpp
class UIView {
public:
    virtual ~UIView() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void on_resize(int w, int h) {}
    virtual void on_draw(AppState& state, Renderer& renderer) = 0;

    void set_bounds(float x, float y, float w, float h);
    float x() const;
    float y() const;
    float w() const;
    float h() const;

protected:
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};
```

**Layout pattern:** The screen's `on_resize()` divides its space and calls `set_bounds()` on each view. Views render within their bounds. No hardcoded pixel values in draw functions.

**Current patterns that become views:**
- Hangar left/right panels → GenomeListView + TopologyPreviewView
- Fly mode game/net panels → GameView + NetPanelView
- Analysis sidebar/chart → ChartSidebarView + ChartView
- Sub-views (NetViewer, TestBench) → UIView subclasses swapped by the screen

## UIModal

Temporary overlays rendered on top of the active screen.

```cpp
class UIModal {
public:
    virtual ~UIModal() = default;

    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual void on_draw(AppState& state, UIManager& ui) = 0;
    virtual const char* name() const = 0;
    virtual bool blocks_input() const { return true; }
};
```

**Note:** `on_draw` takes `UIManager&` so modals can pop themselves or push other modals.

**Reusable subclasses:**

```cpp
class ConfirmModal : public UIModal {
public:
    ConfirmModal(std::string title, std::string message,
                 std::function<void()> on_confirm);
};

class InputModal : public UIModal {
public:
    InputModal(std::string title, std::string prompt,
               std::function<void(const std::string&)> on_submit);
};
```

These replace the free functions in `game-ui/components/modal.h`.

## UIWidget

Free functions in a `ui::` namespace. Consistent styling for common controls.

```cpp
namespace ui {

bool slider_float(const char* label, float* value, float min, float max);
bool input_int(const char* label, int* value, int min, int max);
bool input_text(const char* label, char* buf, size_t buf_size,
                const char* hint = nullptr);

enum class ButtonStyle { Primary, Secondary, Danger };
bool button(const char* label, ButtonStyle style = ButtonStyle::Primary,
            float width = 0.0f);

void section_header(const char* label);
bool checkbox(const char* label, bool* value);

} // namespace ui
```

**Not mandatory.** Screens can use raw ImGui for one-off cases. The `ui::` wrappers are for repeated patterns that should look consistent.

## File Structure

```
neuroflyer/
├── include/neuroflyer/ui/
│   ├── ui_manager.h
│   ├── ui_screen.h
│   ├── ui_view.h
│   ├── ui_modal.h
│   └── ui_widget.h
├── src/ui/
│   ├── framework/
│   │   ├── ui_manager.cpp
│   │   ├── ui_modal.cpp         (ConfirmModal, InputModal)
│   │   └── ui_widget.cpp
│   ├── screens/                 (existing structure)
│   │   ├── menu/
│   │   ├── hangar/
│   │   ├── game/
│   │   └── analysis/
│   ├── views/                   (reusable views)
│   │   ├── genome_list_view.cpp
│   │   ├── topology_preview_view.cpp
│   │   └── ...
│   ├── renderers/
│   ├── components/              (legacy — migrated to views over time)
│   ├── renderer.cpp
│   ├── asset_loader.cpp
│   └── main.cpp
```

## Migration Strategy

Incremental — app builds and runs at every phase boundary.

### Phase 1: Build the framework

Implement UIManager, UIScreen, UIView, UIModal, UIWidget base classes. Wire UIManager into main.cpp replacing the current switch statement. Convert MainMenuScreen as the proof of concept (simplest screen — no views, no state).

### Phase 2: Migrate screens one at a time

Each screen converts from a `draw_*()` free function to a UIScreen subclass. Static locals become member variables. Sub-views become UIView subclasses. Order: MainMenu, Hangar, CreateGenome, VariantViewer, LineageTree, FlySession, PauseConfig, Analysis.

### Phase 3: Extract reusable views

As screens migrate, common patterns (genome list, topology preview, net panel) get pulled into `src/ui/views/` as shared UIView subclasses.

### Phase 4: Replace game-ui modals

Convert `gameui::draw_confirm_modal` / `draw_input_modal` free functions into ConfirmModal / InputModal classes. Delete `game-ui/` library once nothing depends on it.

### Transition support

During migration, UIManager can dispatch both legacy `draw_*()` functions and UIScreen subclasses. A wrapper `LegacyScreen` adapts free functions to the UIScreen interface so migration can happen one screen at a time without breaking the app.
