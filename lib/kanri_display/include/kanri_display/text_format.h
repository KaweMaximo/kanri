#pragma once
// ============================================================================
//  kanri_display/text_format.h — Numeros viram texto, com seguranca
// ============================================================================
//  POR QUE NAO USAR snprintf?
//
//  Duas razoes praticas em firmware:
//
//   1. TAMANHO. O snprintf com suporte a float arrasta ~10 KB de flash e
//      exige o linker de ponto flutuante. Aqui precisamos de "1726" e "83.4",
//      nao de toda a gramatica de formatacao do C.
//   2. PREVISIBILIDADE. Estas funcoes nunca escrevem alem do buffer, sempre
//      terminam em nulo, e nao tem caminho de alocacao. Da para provar isso
//      lendo 40 linhas, o que nao da para fazer com a libc.
//
//  Todas as funcoes seguem o mesmo contrato:
//    - `out` SEMPRE termina em nulo, mesmo em erro ou truncamento;
//    - nunca escrevem mais que `cap` bytes;
//    - devolvem quantos caracteres uteis foram escritos.
//
//  Tudo aqui e funcao pura, sem hardware. E o que permite testar "o que o
//  motorista ve" sem display nenhum.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kanri::display {

/// O que mostrar quando nao ha medida confiavel.
///
/// Isto NAO e enfeite: exibir "0 rpm" quando a rotacao e desconhecida e pior
/// do que exibir "--", porque o motorista acreditaria no zero. Ver
/// docs/SAFETY.md.
constexpr const char* kNoValue = "--";

/// Depois de quanto tempo uma medida deixa de valer como "atual".
///
/// Com o rodizio de 5 PIDs e intervalo de 200 ms, cada grandeza e relida a
/// cada ~1 s. Tres segundos dao folga para uma retentativa sem passar a
/// impressao de que o dado congelou.
constexpr std::uint32_t kMaxValueAgeMs = 3000;

/// Escreve um inteiro com sinal. @return caracteres escritos.
std::size_t format_int(std::int32_t value, char* out, std::size_t cap);

/// Escreve um numero com casas decimais fixas, arredondando.
/// @param decimals 0 a 3.
std::size_t format_fixed(float value, std::uint8_t decimals, char* out,
                         std::size_t cap);

/// Copia texto truncando com seguranca. @return caracteres escritos.
std::size_t copy_text(const char* src, char* out, std::size_t cap);

/// Monta uma linha "rotulo ....... valor unidade", alinhada a direita.
///
/// O alinhamento importa mais do que parece: numeros que mudam de largura
/// (999 -> 1000) pulando na tela sao dificeis de ler de relance, que e como
/// se le um painel dirigindo.
std::size_t format_row(const char* label, const char* value, const char* unit,
                       char* out, std::size_t cap);

// --- Conversoes de unidade -------------------------------------------------
constexpr float celsius_to_fahrenheit(float c) { return c * 9.0F / 5.0F + 32.0F; }
constexpr float kmh_to_mph(float kmh) { return kmh * 0.621371F; }
constexpr float kpa_to_psi(float kpa) { return kpa * 0.145038F; }

}  // namespace kanri::display
