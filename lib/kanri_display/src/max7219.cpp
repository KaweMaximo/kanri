#include "kanri_display/max7219.h"

namespace kanri::display {
namespace {

// Segmentos individuais, na ordem do datasheet (bit 7 = DP).
constexpr std::uint8_t A = 0x40;
constexpr std::uint8_t B = 0x20;
constexpr std::uint8_t C = 0x10;
constexpr std::uint8_t D = 0x08;
constexpr std::uint8_t E = 0x04;
constexpr std::uint8_t F = 0x02;
constexpr std::uint8_t G = 0x01;

struct Glyph {
  char c;
  std::uint8_t bits;
};

// A fonte. Os caracteres sao exatamente os de kAlfabeto em seven_seg.cpp —
// e ha um teste que confronta as duas listas, porque duas fontes de verdade
// sobre "o que da para desenhar" e uma a mais.
//
// Alguns pares sao IDENTICOS por natureza do mostrador, nao por descuido:
// '1' e 'I', '5' e 'S'. Nao ha como distingui-los em 7 segmentos, e fingir
// que ha seria pior.
constexpr Glyph kFont[] = {
    {'0', static_cast<std::uint8_t>(A | B | C | D | E | F)},
    {'1', static_cast<std::uint8_t>(B | C)},
    {'2', static_cast<std::uint8_t>(A | B | G | E | D)},
    {'3', static_cast<std::uint8_t>(A | B | G | C | D)},
    {'4', static_cast<std::uint8_t>(F | G | B | C)},
    {'5', static_cast<std::uint8_t>(A | F | G | C | D)},
    {'6', static_cast<std::uint8_t>(A | F | G | E | C | D)},
    {'7', static_cast<std::uint8_t>(A | B | C)},
    {'8', static_cast<std::uint8_t>(A | B | C | D | E | F | G)},
    {'9', static_cast<std::uint8_t>(A | B | C | D | F | G)},

    {'A', static_cast<std::uint8_t>(A | B | C | E | F | G)},
    {'b', static_cast<std::uint8_t>(F | G | E | C | D)},
    {'C', static_cast<std::uint8_t>(A | F | E | D)},
    {'d', static_cast<std::uint8_t>(B | C | D | E | G)},
    {'E', static_cast<std::uint8_t>(A | F | G | E | D)},
    {'F', static_cast<std::uint8_t>(A | F | G | E)},
    {'H', static_cast<std::uint8_t>(F | G | B | C | E)},
    {'I', static_cast<std::uint8_t>(B | C)},
    {'J', static_cast<std::uint8_t>(B | C | D | E)},
    {'L', static_cast<std::uint8_t>(F | E | D)},
    {'n', static_cast<std::uint8_t>(E | G | C)},
    {'o', static_cast<std::uint8_t>(G | E | C | D)},
    {'P', static_cast<std::uint8_t>(A | B | F | G | E)},
    {'r', static_cast<std::uint8_t>(E | G)},
    {'S', static_cast<std::uint8_t>(A | F | G | C | D)},
    {'t', static_cast<std::uint8_t>(F | G | E | D)},
    {'U', static_cast<std::uint8_t>(B | C | D | E | F)},
    {'y', static_cast<std::uint8_t>(B | C | D | F | G)},

    {'-', G},
    {'_', D},
    {'.', kSegBitDP},
    {' ', 0x00},
};

constexpr std::size_t kFontLen = sizeof(kFont) / sizeof(kFont[0]);

}  // namespace

std::uint8_t encode_char(char c) {
  for (std::size_t i = 0; i < kFontLen; ++i) {
    if (kFont[i].c == c) return kFont[i].bits;
  }
  return 0;
}

bool encode_frame(const SegFrame& frame, std::uint8_t* out, std::size_t cap) {
  if (out == nullptr || cap < kSegDigits) return false;

  std::uint8_t buffer[kSegDigits] = {};
  std::size_t usados = 0;

  for (std::size_t i = 0; i < kSegTextLen && frame.text[i] != '\0'; ++i) {
    const char c = frame.text[i];

    // O ponto monta no digito anterior em vez de ocupar um. E o que permite
    // "13.8" caber em tres digitos.
    if (c == '.' && usados > 0) {
      // Dois pontos seguidos nao existem em nenhum numero. Recusamos em vez
      // de sobrescrever em silencio.
      if ((buffer[usados - 1] & kSegBitDP) != 0) return false;
      buffer[usados - 1] |= kSegBitDP;
      continue;
    }

    if (usados >= kSegDigits) return false;  // nao cabe

    // ' ' desenha nada legitimamente; os outros com bits 0 sao recusa. Sem
    // esta distincao, um caractere invalido viraria um espaco silencioso.
    const std::uint8_t bits = encode_char(c);
    if (bits == 0 && c != ' ') return false;

    buffer[usados] = bits;
    ++usados;
  }

  if (usados == 0) return false;

  // Alinhamento a direita: um "83" num mostrador de tres vira " 83". Um
  // numero encostado a esquerda muda de lugar conforme cresce, e no painel
  // isso e lido como o aparelho piscando.
  const std::size_t offset = kSegDigits - usados;
  for (std::size_t i = 0; i < kSegDigits; ++i) out[i] = 0;
  for (std::size_t i = 0; i < usados; ++i) out[offset + i] = buffer[i];

  return true;
}

std::size_t init_sequence(Max7219Word* out, std::size_t cap,
                          std::uint8_t intensity) {
  if (out == nullptr || cap < kInitWordCount) return 0;

  std::size_t n = 0;
  const auto push = [&](Max7219Reg reg, std::uint8_t data) {
    out[n].reg = static_cast<std::uint8_t>(reg);
    out[n].data = data;
    ++n;
  };

  // Desligado enquanto configuramos. Ao energizar, os registradores do chip
  // estao em estado indefinido — sem isso, o painel pisca lixo no boot.
  push(Max7219Reg::Shutdown, 0x00);

  // O teste de fabrica acende TUDO no brilho maximo. Se ele vier ligado por
  // acaso, seria o pior estado possivel para a alimentacao. Desligamos sempre.
  push(Max7219Reg::DisplayTest, 0x00);

  // Sem decode: nos controlamos cada segmento. Ver o cabecalho do .h.
  push(Max7219Reg::DecodeMode, 0x00);

  // Quantos digitos varrer. Importa alem do obvio: varrer menos digitos da
  // mais ciclo a cada um, entao este valor tambem mexe no brilho.
  push(Max7219Reg::ScanLimit, static_cast<std::uint8_t>(kSegDigits - 1));

  push(Max7219Reg::Intensity,
       intensity > kMaxIntensity ? kMaxIntensity : intensity);

  // Limpa os digitos ANTES de ligar, pelo mesmo motivo do Shutdown.
  for (std::uint8_t d = 0; d < kSegDigits; ++d) {
    out[n].reg = static_cast<std::uint8_t>(Max7219Reg::Digit0) + d;
    out[n].data = 0x00;
    ++n;
  }

  push(Max7219Reg::Shutdown, 0x01);  // agora sim
  return n;
}

std::uint8_t intensity_from_percent(std::uint8_t percent) {
  if (percent > 100) percent = 100;
  // +50 arredonda em vez de truncar: sem isso, 100 % daria 15 mas 99 % daria
  // 14, e a escala inteira ficaria deslocada para baixo.
  return static_cast<std::uint8_t>((percent * kMaxIntensity + 50) / 100);
}

bool blink_visible(std::uint32_t now_ms, std::uint32_t period_ms) {
  if (period_ms == 0) return true;  // periodo zero = nao pisca
  return (now_ms % period_ms) < (period_ms / 2);
}

}  // namespace kanri::display
