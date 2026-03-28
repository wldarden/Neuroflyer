#pragma once

#include <cstddef>
#include <cstdint>

namespace neuroflyer::ui {

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
