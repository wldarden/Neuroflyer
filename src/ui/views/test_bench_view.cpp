#include <neuroflyer/ui/views/test_bench_view.h>

namespace neuroflyer {

void TestBenchView::on_draw(AppState& state, Renderer& renderer, UIManager& /*ui*/) {
    if (networks_) {
        draw_test_bench(state_, *networks_, state.config, renderer);
    }
}

} // namespace neuroflyer
