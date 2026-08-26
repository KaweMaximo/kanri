#pragma once
// ============================================================================
//  kanri_core/version.h — Versao do firmware (FONTE UNICA DA VERDADE)
// ============================================================================
//  Usamos Versionamento Semantico (SemVer): MAIOR.MENOR.CORRECAO
//    MAIOR    incrementa em mudanca incompativel
//    MENOR    incrementa quando entra funcionalidade nova compativel
//    CORRECAO incrementa em correcao de bug
//
//  Enquanto estamos em 0.x.y, a API e considerada instavel — normal para um
//  projeto que ainda esta nascendo.
//
//  AO SUBIR A VERSAO, tres coisas andam JUNTAS (ver CONTRIBUTING.md):
//    1. os numeros aqui;
//    2. uma nova secao no CHANGELOG.md;
//    3. uma tag git anotada `vX.Y.Z` na main.
// ============================================================================

#define KANRI_VERSION_MAJOR 0
#define KANRI_VERSION_MINOR 1
#define KANRI_VERSION_PATCH 0
#define KANRI_VERSION_STRING "0.1.0"

/// Versao empacotada num inteiro, para comparar em codigo:
///   #if KANRI_VERSION_NUMBER >= 000200
#define KANRI_VERSION_NUMBER                                    \
  ((KANRI_VERSION_MAJOR * 10000) + (KANRI_VERSION_MINOR * 100) + \
   KANRI_VERSION_PATCH)
