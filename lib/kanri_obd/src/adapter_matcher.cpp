#include "kanri_obd/adapter_matcher.h"

#include <cstring>

namespace kanri::obd {
namespace {

/// Existe terminador nulo dentro do buffer? Sem isso, qualquer strcmp leria
/// memoria alheia. Nome de dispositivo vem do ar — nao se confia no tamanho.
bool terminado(const char* s, std::size_t cap) {
  for (std::size_t i = 0; i < cap; ++i) {
    if (s[i] == '\0') return true;
  }
  return false;
}

/// Todo caractere e imprimivel? Um nome com bytes de controle nao e um nome
/// legitimo, e aceita-lo abriria espaco para lixo aparecer no display.
bool imprimivel(const char* s, std::size_t cap) {
  for (std::size_t i = 0; i < cap && s[i] != '\0'; ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x20 || c >= 0x7F) return false;
  }
  return true;
}

char maiuscula(char c) {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

/// Comparacao exata ignorando caixa, com limite de tamanho.
///
/// Exata de proposito: "contem" ou "prefixo" fariam "OBDII_FALSO" casar com
/// "OBDII". Num estacionamento, isso e a diferenca entre conectar no
/// adaptador do seu carro e no aparelho de outra pessoa.
///
/// PRE-CONDICAO: `a` tem terminador nulo dentro de `cap`. Quem chama garante
/// isso com utilizavel(). `b` (o alvo configurado) pode nao ter — e por isso
/// o laco respeita `cap` em vez de confiar no terminador.
bool iguais_sem_caixa(const char* a, const char* b, std::size_t cap) {
  for (std::size_t i = 0; i < cap; ++i) {
    const char ca = maiuscula(a[i]);
    const char cb = maiuscula(b[i]);
    if (ca != cb) return false;
    if (ca == '\0') return true;
  }
  // GCOVR_EXCL_START
  // Inalcancavel pelo caminho publico, e mantido como rede de seguranca.
  //
  // Para chegar aqui, NENHUMA das duas strings poderia terminar dentro de
  // `cap`. Mas select_adapter() so chama esta funcao depois de utilizavel(),
  // que ja rejeitou qualquer dispositivo sem terminador — entao `a` sempre
  // termina, e o laco sai por `return true` (iguais) ou `return false`
  // (diferentes) antes do fim.
  //
  // Tentei alcancar por teste e nao ha entrada de select_adapter() que leve
  // aqui. A linha fica porque a funcao e um utilitario: se algum dia alguem
  // chama-la sem passar por utilizavel(), "diferentes" e a resposta segura.
  return false;
  // GCOVR_EXCL_STOP
}

bool vazio(const char* s) { return s == nullptr || s[0] == '\0'; }

/// Um dispositivo so entra na disputa se os campos que vamos comparar forem
/// seguros de ler. Falhar fechado: na duvida, ignora.
bool utilizavel(const DiscoveredDevice& d) {
  return terminado(d.name, kMaxDeviceNameLen) &&
         terminado(d.mac, kMaxDeviceMacLen) &&
         imprimivel(d.name, kMaxDeviceNameLen) &&
         imprimivel(d.mac, kMaxDeviceMacLen);
}

}  // namespace

MatchOutcome select_adapter(const DiscoveredDevice* devices, std::size_t count,
                            const char* target_name, const char* target_mac) {
  MatchOutcome saida;

  if (devices == nullptr || count == 0) {
    saida.result = MatchResult::NoDevices;
    return saida;
  }
  if (vazio(target_name) && vazio(target_mac)) {
    saida.result = MatchResult::NoTarget;
    return saida;
  }

  // -- Regra 1: MAC tem prioridade E e exclusivo -------------------------
  // Quem fixou um MAC quer AQUELE aparelho. Cair para o nome se o MAC nao
  // aparecer anularia justamente a protecao que fixar o MAC oferece.
  if (!vazio(target_mac)) {
    for (std::size_t i = 0; i < count; ++i) {
      if (!utilizavel(devices[i])) continue;
      if (iguais_sem_caixa(devices[i].mac, target_mac, kMaxDeviceMacLen)) {
        saida.result = MatchResult::Found;
        saida.index = static_cast<int>(i);
        return saida;
      }
    }
    saida.result = MatchResult::MacNotFound;
    return saida;
  }

  // -- Regra 2 e 3: nome exato; empate vence o sinal mais forte ----------
  int melhor = -1;
  for (std::size_t i = 0; i < count; ++i) {
    if (!utilizavel(devices[i])) continue;
    if (!iguais_sem_caixa(devices[i].name, target_name, kMaxDeviceNameLen)) {
      continue;
    }
    if (melhor < 0 ||
        devices[i].rssi > devices[static_cast<std::size_t>(melhor)].rssi) {
      melhor = static_cast<int>(i);
    }
  }

  if (melhor >= 0) {
    saida.result = MatchResult::Found;
    saida.index = melhor;
    return saida;
  }
  saida.result = MatchResult::NameNotFound;
  return saida;
}

const char* to_string(MatchResult result) {
  switch (result) {
    case MatchResult::Found:        return "Found";
    case MatchResult::NoDevices:    return "NoDevices";
    case MatchResult::NoTarget:     return "NoTarget";
    case MatchResult::MacNotFound:  return "MacNotFound";
    case MatchResult::NameNotFound: return "NameNotFound";
  }
  return "Unknown";
}

}  // namespace kanri::obd
