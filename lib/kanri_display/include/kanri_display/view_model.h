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

/// Tudo que a tela precisa saber para se desenhar.
///
/// Passamos uma struct, e nao seis parametros soltos, por duas razoes: a
/// ordem de argumentos deixa de ser uma armadilha (dois `uint32_t` seguidos
/// sao faceis de trocar sem o compilador notar), e acrescentar informacao
/// depois nao quebra quem ja chama.
struct ViewContext {
  core::AppState state = core::AppState::Boot;
  const core::TelemetrySnapshot* telemetry = nullptr;
  std::uint32_t now_ms = 0;         ///< Para julgar a idade das medidas.
  bool metric_units = true;         ///< false = mph e Fahrenheit.
  std::uint32_t retry_in_ms = 0;    ///< Quanto falta para a proxima tentativa.
  std::uint32_t retry_attempt = 0;  ///< Numero da tentativa atual.
  const char* adapter_name = "";    ///< Quem estamos procurando.
};

/// Monta a tela a partir do estado do sistema. Funcao PURA — e por isso que
/// da para testar o que o motorista ve sem display nenhum.
///
/// Escolhe a tela pelo estado:
///   Boot, LoadingConfig ....................... Splash
///   Scanning/Connecting/Initializing/Vehicle .. Connecting
///   Polling ................................... Dashboard
///   Degraded, Fault ........................... Error
DisplayFrame build_frame(const ViewContext& context);

}  // namespace kanri::display
