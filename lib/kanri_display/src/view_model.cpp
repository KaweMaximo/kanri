#include "kanri_display/view_model.h"

#include "kanri_core/version.h"

namespace kanri::display {
namespace {

/// Copia texto para um campo de tamanho fixo, sempre terminando em nulo e
/// truncando com seguranca se nao couber.
void set_text(char* dst, const char* src) {
  std::size_t i = 0;
  while (src[i] != '\0' && (i + 1) < kFrameTextLen) {
    dst[i] = src[i];
    ++i;
  }
  for (std::size_t j = i; j < kFrameTextLen; ++j) dst[j] = '\0';
}

}  // namespace

DisplayFrame build_frame(const core::TelemetrySnapshot& telemetry,
                         core::AppState state, bool metric_units) {
  // TODO(v0.3): montar Dashboard/Connecting/Error de verdade, formatando cada
  // TelemetryValue e mostrando "--" quando `valid` for false.
  (void)telemetry;
  (void)metric_units;

  DisplayFrame frame;
  frame.screen = ScreenId::Splash;
  set_text(frame.title, "KANRI");
  set_text(frame.lines[0], KANRI_VERSION_STRING);
  set_text(frame.lines[1], core::to_string(state));
  frame.warning = core::is_error_state(state);
  return frame;
}

}  // namespace kanri::display
