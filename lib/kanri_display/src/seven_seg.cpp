#include "kanri_display/seven_seg.h"

#include "kanri_display/text_format.h"

namespace kanri::display {
namespace {

/// O alfabeto que um display de 7 segmentos consegue desenhar de forma
/// reconhecivel. Fora disto, o resultado e um borrao — e um rotulo ilegivel e
/// pior que nenhum, porque o motorista le a grandeza errada.
constexpr const char* kAlfabeto = "0123456789AbCdEFHIJLnoPrStUy -._";

/// O valor cabe em `uteis` digitos usando `casas` decimais?
///
/// `uteis` e menor que o total quando ha sinal: o menos ocupa uma posicao do
/// mostrador como qualquer digito. Sem descontar isso, -40 virava "-40." —
/// com um ponto solto, porque a casa decimal nao tinha onde caber.
bool cabe(float absoluto, std::uint8_t casas, std::uint8_t uteis) {
  if (casas >= uteis) return false;
  float limite = 1.0F;
  for (std::uint8_t i = 0; i < (uteis - casas); ++i) limite *= 10.0F;
  return absoluto < limite;
}

}  // namespace

const SegMeasure kSegMeasures[] = {
    // Temperatura primeiro: e a grandeza que estraga motor quando ninguem
    // esta olhando. Rotacao e tensao vem em seguida.
    // "tEP", e nao "AGU": o G nao tem forma reconhecivel em 7 segmentos e
    // viraria um borrao. Um rotulo ilegivel e pior que nenhum — o motorista
    // leria a grandeza errada. Ha um teste que cobra isto.
    {"tEP", "coolant_temp", &core::TelemetrySnapshot::coolant_temp_c},
    {"rPn", "engine_rpm", &core::TelemetrySnapshot::engine_rpm},
    {"bAt", "battery_v", &core::TelemetrySnapshot::battery_voltage_v},
    {"UEL", "speed", &core::TelemetrySnapshot::vehicle_speed_kmh},
    {"tPS", "throttle", &core::TelemetrySnapshot::throttle_pct},
    {"Ar", "intake_temp", &core::TelemetrySnapshot::intake_temp_c},
};

const std::size_t kSegMeasureCount =
    sizeof(kSegMeasures) / sizeof(kSegMeasures[0]);

bool format_segments(float value, char* out, std::size_t cap, bool* scaled) {
  if (out == nullptr || cap == 0) return false;
  if (scaled != nullptr) *scaled = false;

  // NaN: a comparacao consigo mesmo e falsa somente para ele.
  if (value != value) {
    copy_text(kSegNoValue, out, cap);
    return false;
  }

  const bool negativo = value < 0.0F;
  float absoluto = negativo ? -value : value;

  // O sinal de menos ocupa um digito inteiro, entao sobra menos espaco para
  // o numero. Nenhuma grandeza do rodizio atual chega a negativo grande
  // (temperatura minima e -40), mas a regra fica correta desde ja.
  const std::uint8_t uteis =
      negativo ? static_cast<std::uint8_t>(kSegDigits - 1) : kSegDigits;

  if (!cabe(absoluto, 0, uteis)) {
    // Nem os digitos inteiros cabem: vai em milhares, como um tacometro.
    absoluto /= 1000.0F;
    if (scaled != nullptr) *scaled = true;
    if (!cabe(absoluto, 0, uteis)) {
      // Nem em milhares cabe: o valor esta fora de qualquer realidade.
      copy_text(kSegNoValue, out, cap);
      return false;
    }
  }

  // Maior precisao que couber. Comeca do maximo e desce.
  std::uint8_t casas = static_cast<std::uint8_t>(uteis - 1);
  while (casas > 0 && !cabe(absoluto, casas, uteis)) --casas;

  char corpo[kSegTextLen + 4];
  format_fixed(negativo ? -absoluto : absoluto, casas, corpo, sizeof(corpo));
  copy_text(corpo, out, cap);
  return true;
}

SegFrame build_seg_frame(const core::TelemetrySnapshot& snapshot,
                         std::size_t index, std::uint32_t now_ms) {
  SegFrame frame;

  if (index >= kSegMeasureCount) {
    copy_text(kSegNoValue, frame.text, sizeof(frame.text));
    return frame;
  }

  const core::TelemetryValue& v = snapshot.*(kSegMeasures[index].field);

  // Mesma regra do resto do projeto: valor nunca lido, ou lido ha tempo
  // demais, nao vira numero no painel.
  if (!v.valid || core::value_age_ms(v, now_ms) > 3000) {
    copy_text(kSegNoValue, frame.text, sizeof(frame.text));
    return frame;
  }

  format_segments(v.value, frame.text, sizeof(frame.text), &frame.scaled);

  // Motor quente pisca. Num mostrador de tres digitos nao ha espaco para
  // escrever um aviso, entao o piscar E o aviso.
  if (kSegMeasures[index].field == &core::TelemetrySnapshot::coolant_temp_c &&
      v.value >= 105.0F) {
    frame.blink = true;
  }
  return frame;
}

bool is_renderable(const char* text) {
  if (text == nullptr) return false;
  for (std::size_t i = 0; text[i] != '\0'; ++i) {
    bool achou = false;
    for (std::size_t j = 0; kAlfabeto[j] != '\0'; ++j) {
      if (text[i] == kAlfabeto[j]) {
        achou = true;
        break;
      }
    }
    if (!achou) return false;
  }
  return true;
}

}  // namespace kanri::display
