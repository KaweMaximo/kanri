#pragma once
// ============================================================================
//  kanri_display/seven_seg.h — O mostrador de 7 segmentos do painel
// ============================================================================
//  Referencia de produto: FuelTech WB-O2 Nano. Um numero grande, no painel,
//  legivel de relance com sol direto. Tres digitos, e uma medida por vez.
//
//  POR QUE ISTO NAO E O DisplayFrame
//  ---------------------------------
//  O DisplayFrame textual tem quatro linhas de 24 caracteres. Aqui cabem TRES
//  DIGITOS. "1726 rpm" nao cabe de jeito nenhum, e nenhuma adaptacao do frame
//  de texto resolveria isso — sao duas saidas com naturezas diferentes, e as
//  duas continuam existindo: o texto serve ao monitor serial e ao painel web,
//  este serve ao painel do carro.
//
//  O PROBLEMA CENTRAL: QUANTAS CASAS DECIMAIS
//  ------------------------------------------
//  Com tres digitos, a precisao possivel depende da grandeza do valor:
//
//      13.8 V   -> "13.8"  (uma casa cabe)
//      9.52 V   -> "9.52"  (duas casas cabem)
//      83 C     -> "83"    (nenhuma casa necessaria)
//      120 km/h -> "120"   (tres digitos cheios)
//      1726 rpm -> nao cabe em digitos inteiros
//
//  A escolha e feita aqui, automaticamente, preferindo sempre a MAIOR precisao
//  que couber. Fixar duas casas para tudo desperdicaria digito na velocidade;
//  fixar zero jogaria fora a precisao da tensao, que e onde ela importa.
//
//  Para o que nao cabe (rotacao), a saida vai em MILHARES — "1.7" para
//  1700 rpm, como num tacometro digital. A ambiguidade com "1.7 V" e resolvida
//  pelo rotulo que aparece antes do valor ao trocar de medida.
//
//  Tudo aqui e funcao pura. Da para testar exatamente o que apareceria no
//  painel sem ter o mostrador montado.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "kanri_core/telemetry.h"

namespace kanri::display {

/// Digitos do mostrador. Trocar por um de 4 digitos muda so esta constante.
constexpr std::uint8_t kSegDigits = 3;

/// Buffer de texto: digitos + sinal + ponto decimal + terminador.
constexpr std::size_t kSegTextLen = kSegDigits + 3;

/// O que aparece quando nao ha medida confiavel. Mesma regra do resto do
/// projeto: nunca um numero em que nao confiamos.
constexpr const char* kSegNoValue = "---";

/// Um retrato do mostrador.
struct SegFrame {
  char text[kSegTextLen] = {};  ///< Ex.: "13.8", "83", "1.7", "---".
  bool blink = false;           ///< Pisca: usado para alerta.
  bool scaled = false;          ///< true = o valor esta em MILHARES.
};

/// Formata um valor para caber no mostrador.
///
/// Escolhe sozinho as casas decimais, preferindo a maior precisao que couber.
/// Se nem os digitos inteiros couberem, divide por mil e marca `scaled`.
///
/// @param out  recebe o texto; sempre terminado em nulo, mesmo em erro.
/// @return false se o valor nao tem representacao util (NaN, absurdo); nesse
///         caso `out` recebe kSegNoValue.
bool format_segments(float value, char* out, std::size_t cap,
                     bool* scaled = nullptr);

// ---------------------------------------------------------------------------
//  As medidas que o botao percorre
// ---------------------------------------------------------------------------

/// Uma grandeza exibivel, com o rotulo curto que a anuncia.
///
/// Um mostrador de 7 segmentos so desenha algumas letras (A b C d E F H I J L
/// n o P r S t U y). Os rotulos abaixo foram escolhidos dentro desse alfabeto
/// e conferidos por teste — um rotulo com letra impossivel viraria um borrao
/// no painel, e o motorista nao saberia qual grandeza esta vendo.
struct SegMeasure {
  const char* label;   ///< Rotulo curto, ate kSegDigits letras.
  const char* key;     ///< Identificador estavel, para log.
  /// Faixa fisica da grandeza (max - min), para a suavizacao saber o que e
  /// mudanca grande. 100 e ruido em rpm e e enorme em graus — ver
  /// kanri_display/smoothing.h.
  float span;
  /// Ponteiro para o campo dentro do snapshot.
  core::TelemetryValue core::TelemetrySnapshot::*field;
};

/// A ordem em que o botao percorre as medidas.
/// Temperatura primeiro: e a que estraga motor quando ninguem olha.
extern const SegMeasure kSegMeasures[];
extern const std::size_t kSegMeasureCount;

/// Teto em tempo de compilacao, para quem precisa dimensionar vetor.
/// Ha um teste conferindo que o catalogo real cabe aqui.
constexpr std::size_t kSegMeasureMax = 12;

/// Monta o que o mostrador deve exibir agora.
///
/// @param snapshot  telemetria atual
/// @param index     qual medida (indice em kSegMeasures)
/// @param now_ms    para julgar a idade da leitura
SegFrame build_seg_frame(const core::TelemetrySnapshot& snapshot,
                         std::size_t index, std::uint32_t now_ms);

/// Todo caractere deste texto pode ser desenhado num display de 7 segmentos?
bool is_renderable(const char* text);

}  // namespace kanri::display
