#pragma once

#include <neuroflyer/arena_session.h>
#include <neuroflyer/base.h>
#include <neuroflyer/camera.h>

#include <SDL.h>

#include <vector>

namespace neuroflyer {

class ArenaGameView {
public:
    explicit ArenaGameView(SDL_Renderer* sdl_renderer);

    void set_bounds(int x, int y, int w, int h) noexcept;

    void render(const ArenaSession& arena, const Camera& camera,
                int follow_index, const std::vector<int>& team_assignments);

private:
    void render_background();
    void render_bases(const std::vector<Base>& bases, const Camera& camera);
    void render_towers(const std::vector<Tower>& towers, const Camera& camera);
    void render_tokens(const std::vector<Token>& tokens, const Camera& camera);
    void render_ships(const std::vector<Triangle>& ships, const Camera& camera,
                      int follow_index, const std::vector<int>& team_assignments);
    void render_bullets(const std::vector<Bullet>& bullets, const Camera& camera);
    void render_follow_indicator(const Triangle& ship, const Camera& camera);

    SDL_Renderer* renderer_;
    int x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};

} // namespace neuroflyer
