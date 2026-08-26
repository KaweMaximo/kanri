#pragma once
// ============================================================================
//  kanri_display/view_model.h — O QUE mostrar (nunca COMO)
// ============================================================================
//  Um erro comum em firmware e espalhar chamadas de desenho pelo codigo todo:
//  `tft.drawString("RPM", 10, 20)` dentro da rotina que le o OBD. Quando isso
//  acontece, trocar o display significa mexer no projeto inteiro, e nao ha
//  como testar a tela sem a tela.
//
//  Aqui a logica produz um DisplayFrame — texto puro, um dado — e alguem
//  (um driver em src/hal) desenha esse dado. Duas consequencias praticas:
//    - da para testar "com o link caido, a tela mostra SEM CONEXAO?" no PC;
//    - trocar OLED por TFT nao toca em uma linha de logica.
//
//  Esse padrao e uma versao enxuta de MVVM / "humble object".
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "kanri_core/state_machine.h"
#include "kanri_core/telemetry.h"

namespace kanri::display {

/// Qual tela esta no ar.
enum class ScreenId : std::uint8_t {
  Splash = 0,   ///< Logo + versao, no boot.
  Connecting,   ///< Progresso de conexao com o adaptador.
  Dashboard,    ///< Operacao normal: as medidas do carro.
  Error,        ///< Estado degradado/falha, com o motivo.
};

constexpr std::size_t kFrameLines = 4;      ///< Linhas de conteudo por tela.
constexpr std::size_t kFrameTextLen = 24;   ///< Caracteres por linha + nulo.

/// Uma tela inteira, pronta para ser desenhada. Tamanho fixo: nenhuma
/// alocacao dinamica no caminho de renderizacao, que roda muitas vezes por
/// segundo.
struct DisplayFrame {
  ScreenId screen = ScreenId::Splash;
  char title[kFrameTextLen] = {};
  char lines[kFrameLines][kFrameTextLen] = {};
  bool warning = false;  ///< Pede destaque visual (inverter, piscar, vermelho).
};

/// Monta a tela a partir do estado do sistema. Funcao PURA — e por isso que
/// da para testar a interface sem hardware nenhum.
///
/// STATUS: esqueleto. Na v0.1 devolve apenas a tela de splash com a versao.
/// A formatacao real das medidas entra na v0.3. Ver docs/ROADMAP.md.
DisplayFrame build_frame(const core::TelemetrySnapshot& telemetry,
                         core::AppState state, bool metric_units);

}  // namespace kanri::display
