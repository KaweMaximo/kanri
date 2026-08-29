#pragma once
// ============================================================================
//  BtSerialTransport — ITransport sobre Bluetooth Classic (SPP)
// ============================================================================
//  Este e o adaptador que finalmente coloca bytes no ar. Ele faz tres coisas:
//
//    1. VARRE o Bluetooth ao redor e monta a lista de dispositivos;
//    2. entrega essa lista para kanri_obd/adapter_matcher.h decidir qual e o
//       nosso adaptador — a decisao e logica pura e testada, nao mora aqui;
//    3. abre o canal serial com o escolhido.
//
//  Repare na divisao: varrer e conectar sao hardware e ficam aqui. ESCOLHER e
//  decisao e fica em lib/, onde ha teste para "dois aparelhos se chamam OBDII"
//  e "o nome veio sem terminador".
//
//  POR QUE BLUETOOTH CLASSIC E NAO BLE: a grande maioria dos adaptadores
//  ELM327 usa SPP (Serial Port Profile), do Bluetooth Classic. So o ESP32
//  original tem esse radio — S3, C3 e C6 sao BLE apenas. Ver docs/HARDWARE.md.
// ============================================================================

#include <BluetoothSerial.h>

#include "kanri_obd/adapter_matcher.h"
#include "kanri_obd/i_transport.h"

namespace kanri::hal {

class BtSerialTransport final : public obd::ITransport {
 public:
  /// @param nome_local  como o ESP32 se apresenta no ar
  BtSerialTransport(const char* nome_local = "Kanri");

  /// Inicializa a pilha Bluetooth em modo mestre. Uma vez, no boot.
  bool begin();

  /// Define quem procurar. Copia as strings: nao guarda ponteiro para fora.
  void set_target(const char* nome, const char* mac, const char* pin);

  // --- Varredura ASSINCRONA ---------------------------------------------
  //
  //  A versao bloqueante (`bt_.discover(5000)`) parece mais simples, mas
  //  quebra duas coisas ao mesmo tempo:
  //
  //   1. o LED CONGELA durante os 5 segundos da busca — justamente quando
  //      ele deveria estar piscando para mostrar que a busca acontece;
  //   2. o loop nao roda, entao o watchdog nao e alimentado. Medido no
  //      hardware: o ESP32 reiniciou 2 vezes em 50 s de operacao.
  //
  //  Com a versao assincrona, o radio trabalha em outra task e o loop
  //  continua girando: LED pisca, watchdog alimentado, tudo responsivo.
  //  E o padrao correto em firmware cooperativo — nenhuma chamada no loop
  //  pode bloquear por mais tempo do que o watchdog tolera.

  /// Dispara a varredura. NAO bloqueia. @return false se nao pode comecar.
  bool scan_start();

  /// Encerra a varredura em andamento e consolida os resultados.
  void scan_stop();

  bool scan_active() const { return varrendo_; }

  /// Resultado da ultima varredura — usado para log e para o matcher.
  const obd::DiscoveredDevice* results() const { return achados_; }
  std::size_t result_count() const { return quantos_; }  // le apos scan_stop()

  /// Aplica as regras de adapter_matcher sobre a ultima varredura.
  obd::MatchOutcome match() const;

  /// Ha um MAC configurado? Nesse caso da para conectar sem varrer.
  bool has_target_mac() const { return alvo_mac_[0] != '\0'; }

  /// Conecta direto no MAC configurado, PULANDO a varredura.
  ///
  /// Resolve um caso real: um adaptador com sinal fraco aparece de forma
  /// intermitente na varredura, ou nao aparece — mas a conexao direta ainda
  /// funciona, porque nao depende de captar o anuncio no exato intervalo em
  /// que a busca acontece. Tambem e mais rapido: dispensa os 5 s de varredura
  /// em toda tentativa.
  bool connect_by_mac();

  // --- ITransport --------------------------------------------------------
  bool connect() override;
  void disconnect() override;
  bool is_connected() const override;
  std::size_t write(const std::uint8_t* data, std::size_t len) override;
  std::size_t read(std::uint8_t* out, std::size_t max_len) override;
  std::size_t available() const override;

 private:
  BluetoothSerial bt_;
  const char* nome_local_;
  bool iniciado_ = false;

  char alvo_nome_[obd::kMaxDeviceNameLen] = {};
  char alvo_mac_[obd::kMaxDeviceMacLen] = {};
  char alvo_pin_[8] = {};

  obd::DiscoveredDevice achados_[obd::kMaxScanResults] = {};

  // volatile: escrito pela task do Bluetooth (dentro do callback da
  // varredura) e lido pelo loop(). A leitura so acontece depois de
  // scan_stop(), quando nao ha mais callbacks em voo.
  volatile std::size_t quantos_ = 0;
  bool varrendo_ = false;
};

}  // namespace kanri::hal
