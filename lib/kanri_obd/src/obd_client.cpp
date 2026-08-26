#include "kanri_obd/obd_client.h"

// ============================================================================
//  ESQUELETO — a implementacao real entra na v0.2 (ver docs/ROADMAP.md).
//
//  Os metodos abaixo compilam, linkam e devolvem uma recusa explicita. Isso e
//  melhor do que nao existirem: a forma da API ja esta fixada e revisada, e
//  quem chamar hoje recebe NotImplemented em vez de um comportamento vago.
//
//  O portao de seguranca, no entanto, JA ESTA ATIVO aqui. Assim a barreira
//  read-only nunca dependeu de "lembrar de adicionar depois".
// ============================================================================

namespace kanri::obd {

ObdClient::ObdClient(ITransport& transport, const core::IClock& clock,
                     const ObdClientConfig& config)
    : transport_(transport), clock_(clock), config_(config) {}

bool ObdClient::initialize() {
  // TODO(v0.2): enviar a sequencia AT (ATZ, ATE0, ATL0, ATS0, ATH0, ATSP0),
  // conferindo cada resposta e passando cada comando por check_at_command().
  return false;
}

ParsedFrame ObdClient::read_pid(std::uint8_t mode, std::uint8_t pid) {
  ParsedFrame frame;

  // SEGURANCA PRIMEIRO: nada e escrito no transporte antes desta checagem.
  const RequestVerdict verdict = check_obd_request(mode, pid);
  if (verdict != RequestVerdict::Allowed) {
    ++rejected_;
    frame.status = ParseStatus::UnknownCommand;
    return frame;
  }

  // TODO(v0.2): montar "%02X%02X\r", escrever no transporte, ler ate o
  // prompt '>' respeitando config_.response_timeout_ms (usando clock_),
  // chamar parse_response() e validar com has_expected_length().
  frame.status = ParseStatus::NotImplemented;
  return frame;
}

bool ObdClient::read_adapter_voltage(float& out_volts) {
  // TODO(v0.2): enviar "ATRV\r" e interpretar a resposta (ex.: "13.8V").
  (void)out_volts;
  return false;
}

}  // namespace kanri::obd
