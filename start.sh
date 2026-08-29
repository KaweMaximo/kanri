#!/usr/bin/env bash
# ============================================================================
#  start.sh — sobe o Kanri Console (painel local de desenvolvimento)
# ============================================================================
#  Uso:
#      ./start.sh                        # sobe e abre o navegador
#      ./start.sh --port 9000            # outra porta HTTP
#      ./start.sh --serial /dev/ttyUSB1  # forca a porta serial
#      ./start.sh --no-browser
#
#  Qualquer argumento e repassado ao server.py.
#
#  O trabalho deste script e um so: achar um Python que tenha `pyserial`.
#  Isso nao e obvio no Ubuntu 24.04+, onde instalar no Python do sistema e
#  bloqueado (PEP 668) e o pyserial normalmente so existe dentro do venv do
#  PlatformIO. Ver CONTRIBUTING.md.
# ============================================================================
set -euo pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVIDOR="$RAIZ/tools/kanri-console/server.py"

if [ ! -f "$SERVIDOR" ]; then
  echo "ERRO: nao encontrei $SERVIDOR" >&2
  exit 1
fi

tem_pyserial() {
  [ -x "$1" ] && "$1" -c 'import serial' >/dev/null 2>&1
}

PYTHON=""
for candidato in \
  "$HOME/.local/share/platformio-venv/bin/python" \
  "$HOME/.platformio/penv/bin/python" \
  "$(command -v python3 || true)" \
  "$(command -v python || true)"
do
  if [ -n "$candidato" ] && tem_pyserial "$candidato"; then
    PYTHON="$candidato"
    break
  fi
done

if [ -z "$PYTHON" ]; then
  cat >&2 <<'AJUDA'

  X Nenhum Python com `pyserial` encontrado.

  O pyserial vem junto com o PlatformIO. Se voce ainda nao instalou:

      sudo apt install pipx && pipx ensurepath
      pipx install platformio

  Ou, sem sudo (ver CONTRIBUTING.md):

      python3 -m venv ~/.local/share/platformio-venv
      ~/.local/share/platformio-venv/bin/pip install platformio gcovr

  Se voce ja tem o PlatformIO em outro lugar, instale o pyserial no Python
  que preferir:

      python3 -m pip install --user pyserial

AJUDA
  exit 1
fi

echo "Kanri Console"
echo "  python : $PYTHON"
exec "$PYTHON" "$SERVIDOR" "$@"
