#pragma once
// ============================================================================
//  kanri_obd/obd_client.h — Cliente OBD2 (ESQUELETO — implementacao na v0.2)
// ============================================================================
//  Junta as pecas: pega um ITransport (o cano), um IClock (o tempo), aplica o
//  portao de seguranca (safety.h), monta o comando, espera a resposta com
//  timeout e devolve um ParsedFrame ja validado (elm327_parser.h).
//
//  STATUS: apenas a interface esta definida. Os metodos existem e compilam,
//  mas devolvem ParseStatus::NotImplemented / false de proposito. Isso e
//  intencional: a v0.1 entrega a ESTRUTURA, e a implementacao vem na v0.2 em
//  sua propria feature branch, com testes. Ver docs/ROADMAP.md.
//
//  Note que a classe recebe as dependencias no construtor por REFERENCIA
//  (injecao de dependencia). Ela nunca cria um BluetoothSerial por conta
//  propria — se criasse, seria impossivel testa-la no PC.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "kanri_core/i_clock.h"
#include "kanri_obd/elm327_parser.h"
#include "kanri_obd/i_transport.h"
#include "kanri_obd/safety.h"

namespace kanri::obd {

/// Parametros de tempo do dialogo com o adaptador.
struct ObdClientConfig {
  std::uint32_t response_timeout_ms = 1000;  ///< Espera maxima por uma resposta.
  std::uint32_t init_timeout_ms = 5000;      ///< Espera maxima na sequencia AT.
  std::uint8_t max_retries = 2;              ///< Retentativas por falha passageira.
};

class ObdClient {
 public:
  ObdClient(ITransport& transport, const core::IClock& clock,
            const ObdClientConfig& config = {});

  /// Executa a sequencia AT de inicializacao do ELM327.
  /// Todo comando enviado passa por check_at_command() antes.
  /// @return true se o adaptador respondeu como esperado.
  bool initialize();

  /// Le um PID. Bloqueia ate a resposta ou o timeout.
  ///
  /// SEGURANCA: chama check_obd_request() antes de escrever qualquer byte no
  /// transporte. Um modo fora de 0x01/0x09 devolve um frame com status de
  /// recusa e NAO toca no barramento.
  ParsedFrame read_pid(std::uint8_t mode, std::uint8_t pid);

  /// Le a tensao medida pelo proprio adaptador (comando ATRV).
  /// Nao envolve a ECU: e o ELM327 medindo o pino 16 do conector.
  /// @param out_volts preenchido somente quando devolve true.
  bool read_adapter_voltage(float& out_volts);

  /// Quantas respostas o parser rejeitou desde o boot. Alimenta a politica
  /// de degradacao e a tela de diagnostico.
  std::uint32_t rejected_count() const { return rejected_; }

 private:
  ITransport& transport_;
  const core::IClock& clock_;
  ObdClientConfig config_;
  std::uint32_t rejected_ = 0;
};

}  // namespace kanri::obd
