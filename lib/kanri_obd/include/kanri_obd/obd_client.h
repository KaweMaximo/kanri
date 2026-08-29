#pragma once
// ============================================================================
//  kanri_obd/obd_client.h — Cliente OBD2 (ESQUELETO — implementacao na v0.2)
// ============================================================================
//  Junta as pecas: pega um ITransport (o cano), um IClock (o tempo), aplica o
//  portao de seguranca (safety.h), monta o comando, espera a resposta com
//  timeout e devolve um ParsedFrame ja validado (elm327_parser.h).
//
//  SOBRE BLOQUEAR: read_pid() espera a resposta chegar, ate o timeout. Isso
//  bloqueia o loop() por, tipicamente, algumas dezenas de milissegundos — e
//  no pior caso (adaptador mudo) por response_timeout_ms.
//
//  E aceitavel porque o pior caso (1 s) esta bem abaixo do watchdog (8 s).
//  Nao seria aceitavel com o valor da varredura Bluetooth, que levava 5 s e
//  fazia a placa reiniciar. A regra pratica do projeto: nenhuma chamada no
//  loop pode bloquear por mais tempo do que o watchdog tolera, e quanto menor,
//  melhor — porque enquanto bloqueia, o LED de status nao atualiza.
//
//  Note que a classe recebe as dependencias no construtor por REFERENCIA
//  (injecao de dependencia). Ela nunca cria um BluetoothSerial por conta
//  propria — se criasse, seria impossivel testa-la no PC.
// ============================================================================

#include <cstddef>
#include <cstdint>

#include "kanri_core/i_clock.h"
#include "kanri_obd/dtc.h"
#include "kanri_obd/elm327_parser.h"
#include "kanri_obd/i_transport.h"
#include "kanri_obd/safety.h"

namespace kanri::obd {

/// Parametros de tempo do dialogo com o adaptador.
struct ObdClientConfig {
  std::uint32_t response_timeout_ms = 1000;  ///< Espera maxima por uma resposta.
  std::uint32_t init_timeout_ms = 5000;      ///< Espera maxima na sequencia AT.
  std::uint8_t max_retries = 2;              ///< Retentativas por falha passageira.

  /// Espera para a PRIMEIRA leitura depois de conectar.
  ///
  /// Com ATSP0 (protocolo automatico), a primeira requisicao nao e so uma
  /// leitura: o ELM327 precisa DESCOBRIR qual protocolo o carro fala,
  /// testando um a um. Isso leva varios segundos.
  ///
  /// Usar o timeout normal aqui faz o firmware desistir no meio da busca e
  /// mandar outro comando — e o ELM327 responde "STOPPED", que foi
  /// exatamente o que aconteceu no primeiro teste com o carro de verdade.
  std::uint32_t first_read_timeout_ms = 12000;
};

class ObdClient {
 public:
  ObdClient(ITransport& transport, const core::IClock& clock,
            const ObdClientConfig& config = {});

  /// Executa a sequencia AT de inicializacao (ver elm327_commands.h).
  /// Todo comando enviado passa por check_at_command() ANTES de ir ao ar.
  /// @return true se todos os passos obrigatorios responderam como esperado.
  bool initialize();

  /// Envia um comando cru e le a resposta ate o prompt '>'.
  ///
  /// Exposto para diagnostico e para os testes. NAO e um atalho para burlar
  /// a allowlist: o comando passa por check_at_command() do mesmo jeito.
  ///
  /// @param out      buffer da resposta; sempre terminado em nulo.
  /// @param cap      tamanho de `out`, incluindo o terminador.
  /// @param timeout_ms  espera maxima; 0 usa config.response_timeout_ms.
  /// @return quantos bytes uteis foram escritos em `out`.
  std::size_t send_at(const char* command, char* out, std::size_t cap,
                      std::uint32_t timeout_ms = 0);

  /// Le um PID. Bloqueia ate a resposta ou o timeout.
  ///
  /// SEGURANCA: chama check_obd_request() antes de escrever qualquer byte no
  /// transporte. Um modo fora de 0x01/0x09 devolve um frame com status de
  /// recusa e NAO toca no barramento.
  ParsedFrame read_pid(std::uint8_t mode, std::uint8_t pid);

  /// Le os codigos de falha de um dos modos de DTC.
  ///
  /// @param kind  Stored (0x03), Pending (0x07) ou Permanent (0x0A).
  /// @return a lista; vazia quando nao ha codigos OU quando a leitura falhou.
  ///         Use `last_dtc_status()` para distinguir os dois casos — "nenhum
  ///         codigo" e uma boa noticia, "nao consegui ler" nao e.
  DtcList read_dtcs(DtcKind kind);

  /// O status da ultima chamada a read_dtcs().
  ParseStatus last_dtc_status() const { return last_dtc_status_; }

  /// Le a tensao medida pelo proprio adaptador (comando ATRV).
  /// Nao envolve a ECU: e o ELM327 medindo o pino 16 do conector.
  /// @param out_volts preenchido somente quando devolve true.
  bool read_adapter_voltage(float& out_volts);

  /// Quantas respostas o parser rejeitou desde o boot. Alimenta a politica
  /// de degradacao e a tela de diagnostico.
  std::uint32_t rejected_count() const { return rejected_; }

  /// Quantas respostas chegaram validas desde o boot.
  std::uint32_t ok_count() const { return ok_; }

  /// true depois de um initialize() bem-sucedido.
  bool ready() const { return ready_; }

  /// Recebe TODO comando pouco antes de ele ir para o transporte.
  ///
  /// Existe para auditoria: com isto ligado, o log mostra literalmente cada
  /// byte que sai para o barramento. A garantia read-only deixa de depender
  /// de confianca no codigo e passa a ser VERIFICAVEL por quem esta olhando
  /// o log — inclusive dentro do carro.
  ///
  /// O gancho e so observador: nao pode alterar nem cancelar o comando. Quem
  /// decide o que pode sair e safety.h, e essa decisao ja aconteceu antes.
  using AuditSink = void (*)(const char* command);
  void set_audit_sink(AuditSink sink) { audit_ = sink; }

  /// Troca os tempos em runtime, sem recriar o cliente.
  /// Usado para aplicar o que veio da configuracao gravada na flash.
  void set_config(const ObdClientConfig& config) { config_ = config; }
  const ObdClientConfig& config() const { return config_; }

 private:
  /// Escreve o comando seguido de CR. @return true se tudo foi aceito.
  bool write_command(const char* command);

  /// Le do transporte ate o prompt '>' ou ate estourar o tempo.
  /// @return bytes escritos em `out` (sempre terminado em nulo).
  std::size_t read_until_prompt(char* out, std::size_t cap,
                                std::uint32_t timeout_ms);

  ITransport& transport_;
  const core::IClock& clock_;
  ObdClientConfig config_;
  std::uint32_t rejected_ = 0;
  std::uint32_t ok_ = 0;
  bool ready_ = false;
  bool primeira_leitura_ = true;
  AuditSink audit_ = nullptr;
  ParseStatus last_dtc_status_ = ParseStatus::Empty;
};

}  // namespace kanri::obd
