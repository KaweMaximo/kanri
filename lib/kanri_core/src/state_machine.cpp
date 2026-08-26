#include "kanri_core/state_machine.h"

namespace kanri::core {

// ---------------------------------------------------------------------------
//  A tabela de transicoes, escrita como switch aninhado.
//
//  Por que switch e nao uma tabela de ponteiros de funcao? Porque switch com
//  enum permite ao compilador avisar (-Wswitch) quando adicionamos um estado
//  novo e esquecemos de tratar. O compilador vira nosso revisor.
//
//  Invariante do desenho: TODO caminho de falha vai para Degraded, nunca para
//  um reset. Somente DisplayFailed leva a Fault, porque sem display nao ha
//  como comunicar nada ao motorista.
// ---------------------------------------------------------------------------
AppState next_state(AppState current, AppEvent event) {
  // Regra global: perder o display e terminal em qualquer estado.
  if (event == AppEvent::DisplayFailed) {
    return AppState::Fault;
  }

  switch (current) {
    case AppState::Boot:
      if (event == AppEvent::HardwareReady) return AppState::LoadingConfig;
      return current;

    case AppState::LoadingConfig:
      // ConfigFailed NAO e fatal: seguimos com os valores padrao. Um firmware
      // que se recusa a ligar por causa de uma configuracao corrompida e
      // inutil no estacionamento.
      if (event == AppEvent::ConfigLoaded || event == AppEvent::ConfigFailed) {
        return AppState::ScanningAdapter;
      }
      return current;

    case AppState::ScanningAdapter:
      if (event == AppEvent::AdapterFound) return AppState::ConnectingAdapter;
      if (event == AppEvent::AdapterNotFound) return AppState::Degraded;
      if (event == AppEvent::AdapterLost) return AppState::Degraded;
      return current;

    case AppState::ConnectingAdapter:
      if (event == AppEvent::AdapterConnected) return AppState::InitializingElm;
      if (event == AppEvent::AdapterLost) return AppState::Degraded;
      if (event == AppEvent::AdapterNotFound) return AppState::Degraded;
      return current;

    case AppState::InitializingElm:
      if (event == AppEvent::ElmReady) return AppState::ConnectingVehicle;
      if (event == AppEvent::ElmFailed) return AppState::Degraded;
      if (event == AppEvent::AdapterLost) return AppState::Degraded;
      return current;

    case AppState::ConnectingVehicle:
      if (event == AppEvent::VehicleLinkUp) return AppState::Polling;
      if (event == AppEvent::VehicleLinkDown) return AppState::Degraded;
      if (event == AppEvent::ElmFailed) return AppState::Degraded;
      if (event == AppEvent::AdapterLost) return AppState::Degraded;
      return current;

    case AppState::Polling:
      // DataInvalid de proposito NAO muda de estado. Uma resposta corrompida
      // isolada e normal no barramento. Quem decide que "muitas falhas
      // seguidas = link caiu" e a camada de orquestracao, que entao emite
      // VehicleLinkDown. Ver docs/ARCHITECTURE.md.
      if (event == AppEvent::VehicleLinkDown) return AppState::Degraded;
      if (event == AppEvent::AdapterLost) return AppState::Degraded;
      if (event == AppEvent::ElmFailed) return AppState::Degraded;
      return current;

    case AppState::Degraded:
      if (event == AppEvent::RetryTimerExpired) return AppState::ScanningAdapter;
      // Atalho: se o canal voltou sozinho, nao precisa varrer de novo.
      if (event == AppEvent::AdapterConnected) return AppState::InitializingElm;
      return current;

    case AppState::Fault:
      // Terminal por decisao de projeto. Sai daqui somente com reset fisico
      // (chave de ignicao / botao). Assim evitamos o loop de reboot.
      return current;
  }

  // Inalcancavel com um enum valido, mas um valor de enum corrompido (por
  // exemplo memoria batida) cai aqui. Fault e o destino seguro.
  return AppState::Fault;
}

const char* to_string(AppState s) {
  switch (s) {
    case AppState::Boot:              return "Boot";
    case AppState::LoadingConfig:     return "LoadingConfig";
    case AppState::ScanningAdapter:   return "ScanningAdapter";
    case AppState::ConnectingAdapter: return "ConnectingAdapter";
    case AppState::InitializingElm:   return "InitializingElm";
    case AppState::ConnectingVehicle: return "ConnectingVehicle";
    case AppState::Polling:           return "Polling";
    case AppState::Degraded:          return "Degraded";
    case AppState::Fault:             return "Fault";
  }
  return "Unknown";
}

const char* to_string(AppEvent e) {
  switch (e) {
    case AppEvent::HardwareReady:     return "HardwareReady";
    case AppEvent::ConfigLoaded:      return "ConfigLoaded";
    case AppEvent::ConfigFailed:      return "ConfigFailed";
    case AppEvent::AdapterFound:      return "AdapterFound";
    case AppEvent::AdapterNotFound:   return "AdapterNotFound";
    case AppEvent::AdapterConnected:  return "AdapterConnected";
    case AppEvent::AdapterLost:       return "AdapterLost";
    case AppEvent::ElmReady:          return "ElmReady";
    case AppEvent::ElmFailed:         return "ElmFailed";
    case AppEvent::VehicleLinkUp:     return "VehicleLinkUp";
    case AppEvent::VehicleLinkDown:   return "VehicleLinkDown";
    case AppEvent::DataValid:         return "DataValid";
    case AppEvent::DataInvalid:       return "DataInvalid";
    case AppEvent::RetryTimerExpired: return "RetryTimerExpired";
    case AppEvent::DisplayFailed:     return "DisplayFailed";
  }
  return "Unknown";
}

}  // namespace kanri::core
