#include "kanri_display/text_format.h"

namespace kanri::display {
namespace {

/// Potencias de 10 para o arredondamento. Tabela em vez de pow(): evita
/// arrastar a biblioteca matematica so por isso.
constexpr float kPotencias[] = {1.0F, 10.0F, 100.0F, 1000.0F};
constexpr std::uint8_t kMaxDecimais = 3;

std::size_t terminar(char* out, std::size_t cap, std::size_t n) {
  if (cap == 0) return 0;
  if (n >= cap) n = cap - 1;
  out[n] = '\0';
  return n;
}

}  // namespace

std::size_t format_int(std::int32_t value, char* out, std::size_t cap) {
  if (out == nullptr || cap == 0) return 0;

  char temp[12];
  std::size_t n = 0;
  const bool negativo = value < 0;

  // Trabalha em 64 bits sem sinal: negar INT32_MIN estouraria o int32.
  std::uint32_t magnitude =
      negativo ? (static_cast<std::uint32_t>(-(value + 1)) + 1U)
               : static_cast<std::uint32_t>(value);

  if (magnitude == 0) {
    temp[n++] = '0';
  } else {
    while (magnitude > 0 && n < sizeof(temp)) {
      temp[n++] = static_cast<char>('0' + (magnitude % 10U));
      magnitude /= 10U;
    }
  }

  std::size_t escritos = 0;
  if (negativo && escritos + 1 < cap) out[escritos++] = '-';
  while (n > 0 && escritos + 1 < cap) out[escritos++] = temp[--n];

  return terminar(out, cap, escritos);
}

std::size_t format_fixed(float value, std::uint8_t decimals, char* out,
                         std::size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (decimals > kMaxDecimais) decimals = kMaxDecimais;

  // NaN e infinito nao tem representacao util num painel. A comparacao
  // `v != v` e verdadeira somente para NaN — o jeito padrao de detecta-lo
  // sem incluir <cmath>.
  if (value != value) return terminar(out, cap, copy_text(kNoValue, out, cap));
  if (value > 2.0e9F || value < -2.0e9F) {
    return copy_text(kNoValue, out, cap);
  }

  const float escala = kPotencias[decimals];
  const bool negativo = value < 0.0F;
  const float absoluto = negativo ? -value : value;

  // +0,5 antes de truncar = arredondamento para o mais proximo.
  const std::uint32_t total =
      static_cast<std::uint32_t>(absoluto * escala + 0.5F);
  const std::uint32_t inteiro = total / static_cast<std::uint32_t>(escala);
  const std::uint32_t fracao = total % static_cast<std::uint32_t>(escala);

  std::size_t n = 0;
  if (negativo && (inteiro != 0 || fracao != 0) && n + 1 < cap) {
    out[n++] = '-';
  }
  n += format_int(static_cast<std::int32_t>(inteiro), out + n,
                  (n < cap) ? (cap - n) : 0);

  if (decimals > 0 && n + 1 < cap) {
    out[n++] = '.';
    // Zeros a esquerda da fracao: 13.05 nao pode virar "13.5".
    std::uint32_t divisor = static_cast<std::uint32_t>(escala) / 10U;
    while (divisor > 0 && n + 1 < cap) {
      out[n++] = static_cast<char>('0' + ((fracao / divisor) % 10U));
      divisor /= 10U;
    }
  }
  return terminar(out, cap, n);
}

std::size_t copy_text(const char* src, char* out, std::size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (src == nullptr) return terminar(out, cap, 0);
  std::size_t n = 0;
  while (src[n] != '\0' && n + 1 < cap) {
    out[n] = src[n];
    ++n;
  }
  return terminar(out, cap, n);
}

std::size_t format_row(const char* label, const char* value, const char* unit,
                       char* out, std::size_t cap) {
  if (out == nullptr || cap == 0) return 0;

  // Mede antes de escrever, para saber quantos pontos cabem no meio.
  std::size_t label_len = 0;
  while (label != nullptr && label[label_len] != '\0') ++label_len;
  std::size_t value_len = 0;
  while (value != nullptr && value[value_len] != '\0') ++value_len;
  std::size_t unit_len = 0;
  while (unit != nullptr && unit[unit_len] != '\0') ++unit_len;

  const std::size_t largura = cap - 1;
  const std::size_t direita = value_len + (unit_len > 0 ? unit_len + 1 : 0);

  // Nao cabe nem rotulo e valor: prioriza o VALOR, que e a informacao.
  if (label_len + 1 + direita > largura) {
    std::size_t n = 0;
    if (direita <= largura && label_len > 0) {
      const std::size_t sobra = largura - direita;
      for (std::size_t i = 0; i < sobra && i < label_len; ++i) out[n++] = label[i];
    }
    while (n + 1 < cap && n + direita < largura) out[n++] = ' ';
    n += copy_text(value, out + n, cap - n);
    if (unit_len > 0 && n + 1 < cap) {
      out[n++] = ' ';
      n += copy_text(unit, out + n, cap - n);
    }
    return terminar(out, cap, n);
  }

  std::size_t n = 0;
  for (std::size_t i = 0; i < label_len && n + 1 < cap; ++i) out[n++] = label[i];

  // Pontinhos ligando rotulo e valor, alinhando a direita.
  const std::size_t pontos = largura - label_len - direita;
  for (std::size_t i = 0; i < pontos && n + 1 < cap; ++i) {
    out[n++] = (i == 0 || i + 1 == pontos) ? ' ' : '.';
  }
  n += copy_text(value, out + n, cap - n);
  if (unit_len > 0 && n + 1 < cap) {
    out[n++] = ' ';
    n += copy_text(unit, out + n, cap - n);
  }
  return terminar(out, cap, n);
}

}  // namespace kanri::display
