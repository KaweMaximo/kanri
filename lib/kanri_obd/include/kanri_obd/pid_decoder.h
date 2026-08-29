#pragma once
// ============================================================================
//  kanri_obd/pid_decoder.h — De bytes crus para grandezas do motor
// ============================================================================
//  A ECU responde numeros sem unidade. Cada PID tem sua propria formula,
//  definida na norma SAE J1979:
//
//      rotacao         = (A * 256 + B) / 4          -> rpm
//      temperatura     =  A - 40                    -> graus Celsius
//      velocidade      =  A                         -> km/h
//      borboleta       =  A * 100 / 255             -> %
//      tensao do modulo= (A * 256 + B) / 1000       -> volts
//
//  O "-40" da temperatura existe porque o byte nao tem sinal: deslocar a
//  escala permite representar de -40 C a +215 C num unico byte.
//
//  DUAS BARREIRAS, NAO UMA
//  -----------------------
//  Aplicar a formula nao basta. Um frame pode passar pelo parser — hexadecimal
//  valido, modo e PID corretos, tamanho certo — e ainda assim conter um valor
//  impossivel. Ruido eletrico no barramento faz exatamente isso.
//
//  Por isso todo valor decodificado passa por uma FAIXA FISICA. O 4B11 do
//  Lancer corta em ~6.500 rpm; uma leitura de 16.383 rpm (o maximo que a
//  formula permite) nao e uma medida, e ruido. Exibir esse numero no painel
//  seria pior do que nao exibir nada — ver docs/SAFETY.md.
//
//  Tudo aqui e funcao pura: entra ParsedFrame, sai numero. Sem hardware.
// ============================================================================

#include <cstdint>

#include "kanri_obd/elm327_parser.h"

namespace kanri::obd {

/// Por que a decodificacao deu certo ou errado.
enum class DecodeStatus : std::uint8_t {
  Ok = 0,
  NotDecodable,   ///< PID sem formula implementada (ainda).
  WrongLength,    ///< O frame nao tem os bytes que a formula precisa.
  OutOfRange,     ///< A formula funcionou, mas o valor e fisicamente impossivel.
  FrameNotOk,     ///< O frame nem chegou valido do parser.
};

/// Um valor decodificado, com procedencia.
struct DecodedValue {
  DecodeStatus status = DecodeStatus::FrameNotOk;
  float value = 0.0F;   ///< Em unidade de engenharia. So use se status == Ok.
  const char* unit = "";  ///< "rpm", "C", "km/h", "%", "V", "kPa", "g/s", "s".

  bool ok() const { return status == DecodeStatus::Ok; }
};

// ---------------------------------------------------------------------------
//  Faixas fisicas aceitas. Valores fora disso sao ruido, nao medida.
//
//  Escolhidas com folga sobre o Mitsubishi Lancer 2.0 2014 (4B11): larga o
//  bastante para nao recusar leitura legitima, apertada o bastante para pegar
//  corrupcao. Ver docs/HARDWARE.md.
// ---------------------------------------------------------------------------
constexpr float kMinRpm = 0.0F;
constexpr float kMaxRpm = 8000.0F;      ///< 4B11 corta em ~6.500
constexpr float kMinTempC = -40.0F;     ///< limite da propria codificacao
constexpr float kMaxTempC = 150.0F;     ///< acima disso o motor ja fundiu
constexpr float kMinSpeedKmh = 0.0F;
constexpr float kMaxSpeedKmh = 255.0F;  ///< limite do byte
constexpr float kMinPercent = 0.0F;
constexpr float kMaxPercent = 100.0F;
constexpr float kMinVolts = 0.0F;
constexpr float kMaxVolts = 30.0F;      ///< rede de 12 V; 30 ja e load dump
constexpr float kMinKpa = 0.0F;
constexpr float kMaxKpa = 255.0F;
constexpr float kMinMaf = 0.0F;
constexpr float kMaxMaf = 655.35F;      ///< limite da formula

/// Decodifica um frame ja validado pelo parser.
///
/// @param frame  precisa ter status Ok; qualquer outro devolve FrameNotOk.
/// @return valor em unidade de engenharia, ou o motivo da recusa.
DecodedValue decode(const ParsedFrame& frame);

/// O PID tem formula implementada?
bool is_decodable(std::uint8_t mode, std::uint8_t pid);

/// Nome legivel do status. Nunca devolve nullptr.
const char* to_string(DecodeStatus status);

}  // namespace kanri::obd
