#pragma once
// ============================================================================
//  kanri_obd/elm327_parser.h — Sanitizacao e parsing das respostas do ELM327
// ============================================================================
//  O ELM327 e um chip serial que fala texto ASCII. Voce manda "010C\r" e ele
//  responde algo como:
//
//      "010C\r41 0C 1A F8\r\r>"
//       ^eco  ^resposta        ^prompt
//
//  Traduzindo a resposta: 41 = "resposta ao modo 01"; 0C = "PID 0C" (RPM);
//  1A F8 = os dados. RPM = (0x1A * 256 + 0xF8) / 4 = 1726 rpm.
//
//  Mas ele tambem responde, com frequencia:
//      "NO DATA"            -> a ECU nao suporta esse PID
//      "SEARCHING..."       -> ainda negociando o protocolo
//      "UNABLE TO CONNECT"  -> ignicao desligada / sem barramento
//      "CAN ERROR"          -> problema eletrico no barramento
//      "BUFFER FULL"        -> perdemos dados
//      "?"                  -> ele nao entendeu o comando
//      "STOPPED"            -> operacao interrompida
//  ...e, em adaptadores clones baratos, simplesmente lixo binario.
//
//  ESTE MODULO E A FRONTEIRA DE CONFIANCA DO FIRMWARE. Nada que venha do
//  adaptador e tratado como valido antes de passar por aqui. Requisito de
//  seguranca — ver docs/SAFETY.md, secao "Validacao de entrada".
//
//  Garantias que este parser oferece:
//    - nunca le fora dos limites do buffer de entrada;
//    - nao usa alocacao dinamica (nada de String, malloc ou new);
//    - nao usa strlen no buffer cru (nao confia em terminador nulo);
//    - rejeita respostas cujo modo/PID nao casam com o que foi pedido
//      (protege contra respostas atrasadas de um pedido anterior);
//    - toda saida vem rotulada com um ParseStatus explicito.
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace kanri::obd {

/// Maior payload aceito (bytes de dados, sem contar o eco de modo+PID).
/// 32 cobre com folga os PIDs do Modo 01; VIN (Modo 09) e multi-frame e
/// sera tratado na v0.2.
constexpr std::size_t kMaxPayloadBytes = 32;

/// Maior resposta crua aceita, em bytes. Acima disso a resposta e descartada
/// sem ser analisada — limite explicito para conter adaptador defeituoso
/// despejando dados sem fim.
constexpr std::size_t kMaxRawResponseBytes = 256;

/// Maior linha (apos limpeza) que ainda tentamos interpretar.
constexpr std::size_t kMaxLineBytes = 96;

/// Todo resultado possivel do parser. Nunca "meio valido".
enum class ParseStatus : std::uint8_t {
  Ok = 0,            ///< Resposta valida, dados em ParsedFrame::data.
  Empty,             ///< Nada util na entrada (so prompt, CR, espacos).
  NoData,            ///< "NO DATA": a ECU nao suporta o PID pedido.
  Searching,         ///< "SEARCHING...": protocolo em negociacao, retentar.
  UnableToConnect,   ///< "UNABLE TO CONNECT": sem barramento / ignicao off.
  BusError,          ///< "CAN ERROR", "BUS ERROR", "ERROR": problema eletrico.
  Stopped,           ///< "STOPPED": operacao abortada pelo adaptador.
  BufferFull,        ///< "BUFFER FULL" ou linha maior que kMaxLineBytes.
  UnknownCommand,    ///< "?": o ELM327 nao entendeu o que mandamos.
  RawTooLong,        ///< Entrada maior que kMaxRawResponseBytes.
  InvalidCharacter,  ///< Caractere que nao e hexadecimal onde deveria ser.
  OddHexDigits,      ///< Numero impar de digitos hex (byte partido pela metade).
  TooShort,          ///< Menos de 2 bytes: nem o eco de modo+PID cabe.
  PayloadTooLong,    ///< Mais dados do que kMaxPayloadBytes.
  UnexpectedMode,    ///< Modo ecoado != modo pedido + 0x40.
  UnexpectedPid,     ///< PID ecoado != PID pedido.
  NotImplemented,    ///< Caminho ainda nao implementado (v0.2).
};

/// Uma resposta ja validada e convertida em bytes.
struct ParsedFrame {
  ParseStatus status = ParseStatus::Empty;
  std::uint8_t mode = 0;    ///< Modo ecoado (0x41 para o modo 01, 0x49 para o 09).
  std::uint8_t pid = 0;     ///< PID ecoado.
  std::uint8_t length = 0;  ///< Bytes uteis em `data`.
  std::uint8_t data[kMaxPayloadBytes] = {};

  /// Unica forma correta de perguntar "posso usar esses dados?".
  bool ok() const { return status == ParseStatus::Ok; }
};

/// Interpreta uma resposta crua do ELM327.
///
/// Processa a entrada LINHA POR LINHA e devolve a primeira linha que for uma
/// resposta hexadecimal valida e casar com o modo/PID esperados. Isso torna o
/// parser tolerante a ruido comum e real: eco do comando, "SEARCHING..."
/// antes do dado, linhas em branco, prompt ">".
///
/// @param raw            buffer cru; NAO precisa terminar em nulo.
/// @param raw_len        tamanho valido de `raw`.
/// @param expected_mode  modo REQUISITADO (0x01 ou 0x09), nao o ecoado.
/// @param expected_pid   PID requisitado.
/// @return frame com status Ok, ou o motivo especifico da recusa.
ParsedFrame parse_response(const char* raw, std::size_t raw_len,
                           std::uint8_t expected_mode,
                           std::uint8_t expected_pid);

/// true quando vale a pena repetir o mesmo pedido (falha passageira).
/// false quando insistir e desperdicio (ex.: a ECU nao tem esse PID).
bool is_transient(ParseStatus status);

/// Nome legivel do status, para log e display. Nunca devolve nullptr.
const char* to_string(ParseStatus status);

}  // namespace kanri::obd
