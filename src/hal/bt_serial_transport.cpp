#include "bt_serial_transport.h"

#include <Arduino.h>

namespace kanri::hal {
namespace {

/// Copia com terminacao garantida. Nunca strncpy: ele NAO garante o
/// terminador quando a origem enche o destino.
void copiar(char* destino, std::size_t cap, const char* origem) {
  if (cap == 0) return;
  std::size_t i = 0;
  if (origem != nullptr) {
    while (origem[i] != '\0' && (i + 1) < cap) {
      destino[i] = origem[i];
      ++i;
    }
  }
  for (std::size_t j = i; j < cap; ++j) destino[j] = '\0';
}

}  // namespace

BtSerialTransport::BtSerialTransport(const char* nome_local)
    : nome_local_(nome_local) {}

bool BtSerialTransport::begin() {
  if (iniciado_) return true;
  // true = modo MESTRE. O ESP32 procura e conecta; o ELM327 e o escravo.
  iniciado_ = bt_.begin(nome_local_, true);
  return iniciado_;
}

void BtSerialTransport::set_target(const char* nome, const char* mac,
                                   const char* pin) {
  copiar(alvo_nome_, sizeof(alvo_nome_), nome);
  copiar(alvo_mac_, sizeof(alvo_mac_), mac);
  copiar(alvo_pin_, sizeof(alvo_pin_), pin);
}

bool BtSerialTransport::scan_start() {
  if (!iniciado_ || varrendo_) return false;
  quantos_ = 0;

  // O callback roda na task do Bluetooth, nao no loop(). Ele so ACRESCENTA
  // ao vetor; a leitura acontece depois de scan_stop(), quando nao ha mais
  // callbacks em voo. Por isso nao e preciso travar nada aqui — e por isso
  // importa nao ler `achados_` enquanto `varrendo_` for verdadeiro.
  const bool ok = bt_.discoverAsync([this](BTAdvertisedDevice* d) {
    if (d == nullptr) return;
    const std::size_t n = quantos_;
    if (n >= obd::kMaxScanResults) return;
    obd::DiscoveredDevice& destino = achados_[n];
    copiar(destino.name, sizeof(destino.name), d->getName().c_str());
    copiar(destino.mac, sizeof(destino.mac), d->getAddress().toString().c_str());
    destino.rssi = static_cast<std::int8_t>(d->getRSSI());
    quantos_ = n + 1;
  });

  varrendo_ = ok;
  return ok;
}

void BtSerialTransport::scan_stop() {
  if (!varrendo_) return;
  bt_.discoverAsyncStop();
  varrendo_ = false;
}

obd::MatchOutcome BtSerialTransport::match() const {
  return obd::select_adapter(achados_, quantos_, alvo_nome_, alvo_mac_);
}

bool BtSerialTransport::connect_by_mac() {
  if (!iniciado_) return false;

  std::uint8_t endereco[6];
  if (!obd::parse_mac(alvo_mac_, endereco)) return false;

  if (alvo_pin_[0] != '\0') bt_.setPin(alvo_pin_);
  return bt_.connect(endereco);
}

bool BtSerialTransport::connect() {
  if (!iniciado_) return false;

  const obd::MatchOutcome escolha = match();
  if (!escolha.found()) return false;

  const obd::DiscoveredDevice& alvo =
      achados_[static_cast<std::size_t>(escolha.index)];

  if (alvo_pin_[0] != '\0') {
    bt_.setPin(alvo_pin_);
  }
  // Conectamos pelo NOME do dispositivo escolhido — e nao pelo nome
  // configurado. Sao a mesma coisa hoje, mas amarrar ao que o matcher
  // realmente escolheu evita divergencia se as regras mudarem.
  return bt_.connect(String(alvo.name));
}

void BtSerialTransport::disconnect() {
  if (iniciado_) bt_.disconnect();
}

bool BtSerialTransport::is_connected() const {
  // BluetoothSerial::connected() nao e const na API do Arduino.
  return const_cast<BluetoothSerial&>(bt_).connected();
}

std::size_t BtSerialTransport::write(const std::uint8_t* data,
                                     std::size_t len) {
  if (!is_connected() || data == nullptr) return 0;
  return bt_.write(data, len);
}

std::size_t BtSerialTransport::read(std::uint8_t* out, std::size_t max_len) {
  if (out == nullptr || max_len == 0) return 0;
  std::size_t lidos = 0;
  while (lidos < max_len && bt_.available() > 0) {
    const int c = bt_.read();
    if (c < 0) break;
    out[lidos++] = static_cast<std::uint8_t>(c);
  }
  return lidos;
}

std::size_t BtSerialTransport::available() const {
  return static_cast<std::size_t>(
      const_cast<BluetoothSerial&>(bt_).available());
}

}  // namespace kanri::hal
