#pragma once

#include <neuroflyer/ui/ui_view.h>
#include <neuroflyer/components/test_bench.h>

#include <neuralnet/network.h>

#include <vector>

namespace neuroflyer {

/// UIView wrapper around the TestBench component.
/// Owns a TestBenchState and delegates drawing to draw_test_bench().
class TestBenchView : public UIView {
public:
    void on_draw(AppState& state, Renderer& renderer, UIManager& ui) override;

    TestBenchState& bench_state() { return state_; }
    [[nodiscard]] bool wants_save() const { return state_.wants_save; }
    [[nodiscard]] bool wants_cancel() const { return state_.wants_cancel; }

    /// Set the networks vector for the test bench to use.
    void set_networks(std::vector<neuralnet::Network>* networks) { networks_ = networks; }

private:
    TestBenchState state_;
    std::vector<neuralnet::Network>* networks_ = nullptr;
};

} // namespace neuroflyer
