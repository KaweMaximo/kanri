#include "kanri_display/view_model.h"

#include "kanri_core/version.h"
#include "kanri_display/text_format.h"

namespace kanri::display {
namespace {

void set_text(char* dst, const char* src) { copy_text(src, dst, kFrameTextLen); }

/// Monta uma linha de medida, ja tratando os tres casos que importam:
/// nunca lida, lida ha muito tempo, e lida agora.
///
/// A regra e a mesma de docs/SAFETY.md: um valor em que nao confiamos vira
/// "--", nunca um numero. Mostrar "0 rpm" quando a rotacao e desconhecida
/// seria pior do que nao mostrar nada, porque o motorista acreditaria.
void measure_row(char* dst, const char* label, const core::TelemetryValue& v,
                 const char* unit, std::uint8_t decimals,
                 std::uint32_t now_ms) {
  char valor[12];

  if (!v.valid || core::value_age_ms(v, now_ms) > kMaxValueAgeMs) {
    copy_text(kNoValue, valor, sizeof(valor));
    // Sem unidade junto do "--": "-- rpm" sugere uma medida que nao existe.
    format_row(label, valor, "", dst, kFrameTextLen);
    return;
  }

  format_fixed(v.value, decimals, valor, sizeof(valor));
  format_row(label, valor, unit, dst, kFrameTextLen);
}

/// Converte para a unidade escolhida antes de formatar.
core::TelemetryValue converted(const core::TelemetryValue& v, bool metric,
                              float (*conv)(float)) {
  if (metric || !v.valid) return v;
  core::TelemetryValue saida = v;
  saida.value = conv(v.value);
  return saida;
}

}  // namespace

DisplayFrame build_frame(const ViewContext& ctx) {
  DisplayFrame frame;
  const bool metrico = ctx.metric_units;

  switch (ctx.state) {
    // -- Splash: quem sou eu e que versao ---------------------------------
    case core::AppState::Boot:
    case core::AppState::LoadingConfig: {
      frame.screen = ScreenId::Splash;
      set_text(frame.title, "KANRI");
      set_text(frame.lines[0], KANRI_VERSION_STRING);
      set_text(frame.lines[1], "somente leitura");
      set_text(frame.lines[2], core::to_string(ctx.state));
      break;
    }

    // -- Connecting: em que ponto da conexao estamos -----------------------
    case core::AppState::ScanningAdapter:
    case core::AppState::ConnectingAdapter:
    case core::AppState::InitializingElm:
    case core::AppState::ConnectingVehicle: {
      frame.screen = ScreenId::Connecting;
      set_text(frame.title, "CONECTANDO");

      // O rotulo sai deste switch, e nao de uma funcao auxiliar. Uma auxiliar
      // precisaria de um `default:` para os estados que nunca chegam aqui —
      // um caminho que nenhum teste alcanca e que so existe para calar o
      // compilador. Aqui o switch externo ja garante a exaustividade.
      const char* etapa = "Procurando...";
      if (ctx.state == core::AppState::ConnectingAdapter) {
        etapa = "Conectando...";
      } else if (ctx.state == core::AppState::InitializingElm) {
        etapa = "Iniciando ELM...";
      } else if (ctx.state == core::AppState::ConnectingVehicle) {
        etapa = "Falando c/ ECU...";
      }
      set_text(frame.lines[0], etapa);
      // Mostrar quem procuramos ajuda a perceber na hora que o nome
      // configurado esta errado — o erro mais comum na primeira instalacao.
      if (ctx.adapter_name != nullptr && ctx.adapter_name[0] != '\0') {
        format_row("alvo", ctx.adapter_name, "", frame.lines[1], kFrameTextLen);
      }
      if (ctx.retry_attempt > 0) {
        char n[12];
        format_int(static_cast<std::int32_t>(ctx.retry_attempt), n, sizeof(n));
        format_row("tentativa", n, "", frame.lines[2], kFrameTextLen);
      }
      break;
    }

    // -- Dashboard: as medidas do carro ------------------------------------
    case core::AppState::Polling: {
      frame.screen = ScreenId::Dashboard;
      set_text(frame.title, "KANRI");
      if (ctx.telemetry == nullptr) break;
      const core::TelemetrySnapshot& t = *ctx.telemetry;

      measure_row(frame.lines[0], "RPM", t.engine_rpm, "", 0, ctx.now_ms);
      measure_row(frame.lines[1], "Agua",
                  converted(t.coolant_temp_c, metrico, celsius_to_fahrenheit),
                  metrico ? "C" : "F", 0, ctx.now_ms);
      measure_row(frame.lines[2], "Vel",
                  converted(t.vehicle_speed_kmh, metrico, kmh_to_mph),
                  metrico ? "km/h" : "mph", 0, ctx.now_ms);
      measure_row(frame.lines[3], "Bateria", t.battery_voltage_v, "V", 1,
                  ctx.now_ms);

      // Aviso visual quando o motor esta esquentando demais. 105 C ja e
      // territorio de atencao num 4B11; e a informacao que o motorista
      // precisa notar sem procurar.
      if (t.coolant_temp_c.valid &&
          core::value_age_ms(t.coolant_temp_c, ctx.now_ms) <= kMaxValueAgeMs &&
          t.coolant_temp_c.value >= 105.0F) {
        frame.warning = true;
      }
      break;
    }

    // -- Error: o que houve e o que vai acontecer --------------------------
    case core::AppState::Degraded: {
      frame.screen = ScreenId::Error;
      set_text(frame.title, "SEM CONEXAO");
      set_text(frame.lines[0], "adaptador nao achado");
      if (ctx.retry_attempt > 0) {
        char n[12];
        format_int(static_cast<std::int32_t>(ctx.retry_attempt), n, sizeof(n));
        format_row("tentativa", n, "", frame.lines[1], kFrameTextLen);
      }
      if (ctx.retry_in_ms > 0) {
        char s[12];
        // Em segundos: "8 s" se le de relance; "8000 ms" nao.
        format_fixed(static_cast<float>(ctx.retry_in_ms) / 1000.0F, 0, s,
                     sizeof(s));
        format_row("retry em", s, "s", frame.lines[2], kFrameTextLen);
      }
      frame.warning = true;
      break;
    }

    case core::AppState::Fault: {
      frame.screen = ScreenId::Error;
      set_text(frame.title, "FALHA");
      set_text(frame.lines[0], "display nao respondeu");
      set_text(frame.lines[1], "reinicie o aparelho");
      frame.warning = true;
      break;
    }
  }

  return frame;
}


}  // namespace kanri::display
