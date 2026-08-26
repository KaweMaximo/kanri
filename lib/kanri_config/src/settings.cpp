#include "kanri_config/settings.h"

namespace kanri::config {
namespace {

/// Copia com terminacao garantida e o resto do buffer zerado.
///
/// Por que nao strncpy? Porque strncpy NAO garante o terminador quando a
/// origem enche o destino — uma das causas classicas de leitura fora dos
/// limites em C. Zerar o resto tambem deixa a struct deterministica, o que
/// importa quando ela e gravada byte a byte na flash.
void set_string(char* dst, std::size_t cap, const char* src) {
  if (cap == 0) return;
  std::size_t i = 0;
  while (src[i] != '\0' && (i + 1) < cap) {
    dst[i] = src[i];
    ++i;
  }
  for (std::size_t j = i; j < cap; ++j) dst[j] = '\0';
}

/// Existe um terminador nulo dentro do buffer? Se nao, a flash esta corrompida
/// e qualquer strlen/strcmp nessa string leria memoria alheia.
bool is_terminated(const char* s, std::size_t cap) {
  for (std::size_t i = 0; i < cap; ++i) {
    if (s[i] == '\0') return true;
  }
  return false;
}

bool is_hex_char(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

/// Aceita exatamente "XX:XX:XX:XX:XX:XX" (17 caracteres + nulo).
bool is_valid_mac(const char* mac) {
  for (std::size_t i = 0; i < 17; ++i) {
    if (mac[i] == '\0') return false;
    if ((i % 3) == 2) {
      if (mac[i] != ':') return false;
    } else if (!is_hex_char(mac[i])) {
      return false;
    }
  }
  return mac[17] == '\0';
}

std::uint16_t clamp_u16(std::uint16_t value, std::uint16_t low,
                        std::uint16_t high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

}  // namespace

KanriSettings default_settings() {
  KanriSettings settings{};  // zera tudo antes de preencher
  settings.schema_version = kSettingsSchemaVersion;
  // "OBDII" e o nome de fabrica da maioria dos clones ELM327 vendidos por ai.
  set_string(settings.adapter_name, kAdapterNameLen, "OBDII");
  set_string(settings.adapter_mac, kAdapterMacLen, "");
  set_string(settings.adapter_pin, kAdapterPinLen, "1234");
  settings.poll_interval_ms = 200;
  settings.elm_timeout_ms = 1000;
  settings.display_brightness = 80;
  settings.use_metric_units = 1;  // carro brasileiro: km/h e Celsius
  return settings;
}

SettingsError validate(const KanriSettings& settings) {
  if (settings.schema_version != kSettingsSchemaVersion) {
    return SettingsError::SchemaMismatch;
  }
  // Terminacao primeiro: sem isso, nao e seguro nem LER as strings abaixo.
  if (!is_terminated(settings.adapter_name, kAdapterNameLen) ||
      !is_terminated(settings.adapter_mac, kAdapterMacLen) ||
      !is_terminated(settings.adapter_pin, kAdapterPinLen)) {
    return SettingsError::AdapterNameNotTerminated;
  }
  // Sem nome E sem MAC nao ha como localizar o adaptador.
  if (settings.adapter_name[0] == '\0' && settings.adapter_mac[0] == '\0') {
    return SettingsError::AdapterNameEmpty;
  }
  if (settings.adapter_mac[0] != '\0' && !is_valid_mac(settings.adapter_mac)) {
    return SettingsError::MalformedMac;
  }
  if (settings.poll_interval_ms < kMinPollIntervalMs ||
      settings.poll_interval_ms > kMaxPollIntervalMs) {
    return SettingsError::PollIntervalOutOfRange;
  }
  if (settings.elm_timeout_ms < kMinElmTimeoutMs ||
      settings.elm_timeout_ms > kMaxElmTimeoutMs) {
    return SettingsError::ElmTimeoutOutOfRange;
  }
  if (settings.display_brightness > kMaxBrightness) {
    return SettingsError::BrightnessOutOfRange;
  }
  if (settings.use_metric_units > 1) {
    return SettingsError::InvalidFlag;
  }
  return SettingsError::None;
}

bool clamp_to_valid(KanriSettings& settings) {
  bool changed = false;

  // Esquema diferente = layout de bytes desconhecido. Nao ha como confiar em
  // campo nenhum: voltamos inteiro para o padrao de fabrica.
  if (settings.schema_version != kSettingsSchemaVersion) {
    settings = default_settings();
    return true;
  }

  // Forca a terminacao ANTES de olhar o conteudo das strings.
  if (!is_terminated(settings.adapter_name, kAdapterNameLen)) {
    settings.adapter_name[kAdapterNameLen - 1] = '\0';
    changed = true;
  }
  if (!is_terminated(settings.adapter_mac, kAdapterMacLen)) {
    settings.adapter_mac[kAdapterMacLen - 1] = '\0';
    changed = true;
  }
  if (!is_terminated(settings.adapter_pin, kAdapterPinLen)) {
    settings.adapter_pin[kAdapterPinLen - 1] = '\0';
    changed = true;
  }

  if (settings.adapter_name[0] == '\0' && settings.adapter_mac[0] == '\0') {
    set_string(settings.adapter_name, kAdapterNameLen, "OBDII");
    changed = true;
  }
  // MAC ruim: descartamos e voltamos a casar pelo nome, que ainda funciona.
  if (settings.adapter_mac[0] != '\0' && !is_valid_mac(settings.adapter_mac)) {
    set_string(settings.adapter_mac, kAdapterMacLen, "");
    if (settings.adapter_name[0] == '\0') {
      set_string(settings.adapter_name, kAdapterNameLen, "OBDII");
    }
    changed = true;
  }

  const std::uint16_t poll = clamp_u16(settings.poll_interval_ms,
                                       kMinPollIntervalMs, kMaxPollIntervalMs);
  if (poll != settings.poll_interval_ms) {
    settings.poll_interval_ms = poll;
    changed = true;
  }

  const std::uint16_t timeout =
      clamp_u16(settings.elm_timeout_ms, kMinElmTimeoutMs, kMaxElmTimeoutMs);
  if (timeout != settings.elm_timeout_ms) {
    settings.elm_timeout_ms = timeout;
    changed = true;
  }

  if (settings.display_brightness > kMaxBrightness) {
    settings.display_brightness = kMaxBrightness;
    changed = true;
  }

  // Normaliza a flag: qualquer byte diferente de zero vira 1.
  if (settings.use_metric_units > 1) {
    settings.use_metric_units = 1;
    changed = true;
  }

  return changed;
}

const char* to_string(SettingsError error) {
  switch (error) {
    case SettingsError::None:                     return "None";
    case SettingsError::SchemaMismatch:           return "SchemaMismatch";
    case SettingsError::AdapterNameEmpty:         return "AdapterNameEmpty";
    case SettingsError::AdapterNameNotTerminated: return "AdapterNameNotTerminated";
    case SettingsError::PollIntervalOutOfRange:   return "PollIntervalOutOfRange";
    case SettingsError::ElmTimeoutOutOfRange:     return "ElmTimeoutOutOfRange";
    case SettingsError::BrightnessOutOfRange:     return "BrightnessOutOfRange";
    case SettingsError::MalformedMac:             return "MalformedMac";
    case SettingsError::InvalidFlag:              return "InvalidFlag";
  }
  return "Unknown";
}

}  // namespace kanri::config
