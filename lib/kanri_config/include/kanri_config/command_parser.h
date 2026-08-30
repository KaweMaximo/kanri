#pragma once
// ============================================================================
//  kanri_config/command_parser.h — Comandos de configuracao pelo serial
// ============================================================================
//  PARA QUE ISSO EXISTE
//
//  O nome Bluetooth do adaptador ELM327 varia: "OBDII" e o mais comum, mas
//  existem "OBDII_V", "Android-Vlink", "V-LINK", "OBD2". Sem uma forma de
//  configurar em runtime, descobrir que o seu se chama diferente significaria
//  recompilar e regravar o firmware — dentro do carro, com o notebook no colo.
//
//  Com estes comandos, e digitar uma linha:
//
//      set nome V-LINK
//      save
//      reiniciar
//
//  A configuracao vai para a flash (NVS) e sobrevive ao desligamento.
//
//  ONDE A LOGICA MORA
//  ------------------
//  Interpretar a linha e decisao, entao mora aqui, em lib/, com teste. Ler do
//  Serial e aplicar o resultado e hardware, e mora no main.cpp. Assim da para
//  testar "o que acontece se alguem digitar `set intervalo abc`" sem ESP32.
//
//  ENTRADA HOSTIL, DE NOVO: o que chega pelo serial e digitado por uma pessoa
//  — com espaco a mais, sem argumento, com valor absurdo, ou colado de um
//  lugar qualquer com caracteres estranhos. Tratamos como qualquer outra
//  entrada externa.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "kanri_config/settings.h"

namespace kanri::config {

/// Maior linha de comando aceita. Acima disso, recusamos sem interpretar.
constexpr std::size_t kMaxCommandLen = 96;
/// Maior valor de argumento (cabe um nome de adaptador ou um MAC).
constexpr std::size_t kMaxArgLen = 40;

/// O que o comando pede.
enum class CommandAction : std::uint8_t {
  None = 0,       ///< Linha vazia: nada a fazer, e nao e erro.
  Help,           ///< Lista os comandos.
  Status,         ///< Mostra estado e configuracao atuais.
  Scan,           ///< Forca uma nova varredura Bluetooth.
  Save,           ///< Grava a configuracao na flash.
  Load,           ///< Recarrega a configuracao da flash.
  Defaults,       ///< Volta aos valores de fabrica (nao grava sozinho).
  Restart,        ///< Reinicia o ESP32.
  ReadDtc,        ///< Le os codigos de falha da ECU.
  SetName,        ///< Nome Bluetooth do adaptador.
  SetMac,         ///< MAC do adaptador ("" limpa).
  SetPin,         ///< PIN de pareamento.
  SetInterval,    ///< Intervalo entre leituras, em ms.
  SetTimeout,     ///< Espera maxima por resposta, em ms.
  SetBrightness,  ///< Brilho do display, 0-100.
  SetUnits,       ///< "metrico" ou "imperial".
  SegTest,        ///< Autoteste do mostrador de 7 segmentos.
  SegShow,        ///< Escreve um texto direto no mostrador (e SEGURA a tela).
  SegAuto,        ///< Devolve o mostrador a telemetria.
  PotStatus,      ///< Mostra a leitura crua do potenciometro de brilho.
  GpioWrite,      ///< Aciona um GPIO livre: `gpio <pino> <0|1>`.
  LedBar,         ///< Define a barra de LEDs: `leds 22,21,19`.
  LedBlink,       ///< Liga/desliga o piscar da barra.
  DigitWrite,     ///< Aciona um digito sobrando do MAX7219: `dig 4 255`.
};

/// Por que a linha nao pode ser executada.
enum class CommandError : std::uint8_t {
  None = 0,
  TooLong,        ///< Linha maior que kMaxCommandLen.
  UnknownCommand,
  MissingArgument,
  ArgumentTooLong,
  InvalidNumber,  ///< Esperava numero e veio outra coisa.
  InvalidValue,   ///< Numero valido, mas fora da faixa aceita.
};

/// O resultado de interpretar uma linha.
struct ParsedCommand {
  CommandAction action = CommandAction::None;
  CommandError error = CommandError::None;
  char text[kMaxArgLen] = {};  ///< Argumento textual, quando houver.
  std::uint32_t number = 0;    ///< Argumento numerico, quando houver.
  /// Segundo argumento numerico. Usado por `gpio <pino> <valor>`, onde os
  /// dois numeros tem significados diferentes e juntar num so seria pedir
  /// para alguem confundir a ordem.
  std::uint32_t number2 = 0;

  bool ok() const { return error == CommandError::None; }
};

/// Interpreta uma linha digitada.
///
/// Aceita, de proposito, formas relaxadas: espacos extras, caixa mista no
/// comando (mas NAO no valor — um nome Bluetooth diferencia maiusculas), e
/// tanto `set nome X` quanto `nome X`.
///
/// @param line  a linha; NAO precisa terminar em nulo.
/// @param len   tamanho valido de `line`.
ParsedCommand parse_command(const char* line, std::size_t len);

/// Aplica um comando ja interpretado sobre uma configuracao.
///
/// So mexe em `settings` se o comando for de escrita e estiver correto.
/// Depois de aplicar, a configuracao continua valida — os limites de
/// settings.h sao respeitados.
///
/// @return true se `settings` foi alterado.
bool apply_command(const ParsedCommand& command, KanriSettings& settings);

/// Nomes legiveis. Nunca devolvem nullptr.
const char* to_string(CommandAction action);
const char* to_string(CommandError error);

/// Texto de ajuda, uma linha por comando. Termina com nullptr.
/// Uma palavra aceita pelo console e a acao que ela dispara.
struct CommandWord {
  const char* word;
  CommandAction action;
};

/// Todas as palavras aceitas, terminadas por uma entrada com `word` nulo.
///
/// Existe para o teste cobrar que toda ACAO apareca em help_lines(). Nao toda
/// palavra: `help` e `ajuda` fazem a mesma coisa, e documentar as duas seria
/// ruido. Mas uma acao que nenhuma palavra dela aparece na ajuda e uma acao
/// que ninguem descobre — foi o que aconteceu com `teste`, `seg` e `auto`.
const CommandWord* command_words();

const char* const* help_lines();

}  // namespace kanri::config
