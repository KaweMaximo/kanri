# Contribuindo com o Kanri

Antes de tudo, leia:
- **[docs/SAFETY.md](docs/SAFETY.md)** — requisitos de segurança
- **[docs/TESTING.md](docs/TESTING.md)** — política de testes (obrigatória)
- **[CLAUDE.md](CLAUDE.md)** — convenções

---

## Preparando o ambiente

### Instalando o PlatformIO

> ⚠️ **`pip install platformio` NÃO funciona** no Ubuntu 24.04+, Debian 12+ e
> Fedora recentes. Essas distribuições marcam o Python do sistema como
> *externally managed* ([PEP 668](https://peps.python.org/pep-0668/)) e
> recusam instalação global:
>
> ```
> error: externally-managed-environment
> × This environment is externally managed
> ```
>
> Isso é proteção, não bug: instalar pacote no Python do sistema pode quebrar
> ferramentas da própria distribuição.

Escolha **uma** das opções abaixo.

#### Opção A — `pipx` (mais idiomática; precisa de `sudo` uma vez)

```bash
sudo apt install pipx
pipx ensurepath          # coloca ~/.local/bin no PATH
pipx install platformio
pipx install gcovr       # opcional, para medir cobertura
```

#### Opção B — venv dedicado, sem `sudo` (testada neste projeto)

É o que o `pipx` faz por baixo, feito à mão. Não toca no Python do sistema.

```bash
python3 -m venv ~/.local/share/platformio-venv
~/.local/share/platformio-venv/bin/pip install platformio gcovr

# Deixa `pio` e `gcovr` disponíveis em qualquer terminal
mkdir -p ~/.local/bin
ln -sf ~/.local/share/platformio-venv/bin/pio        ~/.local/bin/pio
ln -sf ~/.local/share/platformio-venv/bin/platformio ~/.local/bin/platformio
ln -sf ~/.local/share/platformio-venv/bin/gcovr      ~/.local/bin/gcovr

pio --version            # confirme que responde
```

Se `pio` não for encontrado, `~/.local/bin` não está no seu `PATH`. Acrescente
ao `~/.zshrc` (ou `~/.bashrc`):

```bash
export PATH="$HOME/.local/bin:$PATH"
```

**Para desinstalar:** `rm -rf ~/.local/share/platformio-venv ~/.local/bin/{pio,platformio,gcovr}`

#### Opção C — extensão do VS Code

Instale **PlatformIO IDE** no VS Code. Ela baixa e gerencia o PlatformIO Core
sozinha, num diretório próprio. Bom se você for trabalhar só pelo editor — mas
você não terá o comando `pio` no terminal.

#### ❌ O que evitar

```bash
pip install --break-system-packages platformio   # pode quebrar o Python do SO
sudo pip install platformio                      # pior ainda
```

### Preparando o clone

```bash
git clone git@github.com:KaweMaximo/kanri.git
cd kanri

# Ativa o hook que valida a mensagem de commit localmente.
# Faça isso uma vez por clone — o Git não versiona a configuração de hooks.
git config core.hooksPath .githooks

pio pkg install                 # baixa dependências (Unity, plataformas)
pio test -e native              # 122 casos verdes
pio run -e esp32dev             # compila o firmware
```

Na primeira execução o PlatformIO baixa o toolchain do ESP32 (~200 MB). Depois
disso, o build leva segundos.

---

## Fluxo de trabalho

```bash
# 1. Sempre parta da main atualizada
git switch main
git pull

# 2. Crie a branch (nunca trabalhe na main)
git switch -c feat/leitura-de-rpm

# 3. Trabalhe, em commits pequenos e com mensagem clara
git add lib/kanri_obd/src/obd_client.cpp
git commit -m "feat(obd): implementa envio de comando com timeout"

# 4. Antes de subir, valide localmente
pio test -e native
pio run -e esp32dev

# 5. Suba e abra o PR
git push -u origin feat/leitura-de-rpm
gh pr create
```

### Regras

| Regra | Por quê |
|-------|---------|
| Nunca commite direto na `main` | A `main` tem que estar sempre compilando e testada |
| Uma branch por assunto | PR pequeno é revisado de verdade; PR grande é aprovado sem ler |
| Merge só via PR com CI verde | O CI é a rede de segurança, não uma formalidade |
| Código novo em `lib/` chega com teste | Cobrado pelo CI: 100% de cobertura de linhas. Ver [TESTING.md](docs/TESTING.md) |
| Mudança de código ou infra atualiza o `CHANGELOG.md` | Cobrado pelo CI. Um changelog que só é lembrado na hora do release já nasceu incompleto |
| Commits em Conventional Commits | Permite gerar CHANGELOG e deixa o `git log` legível |

---

## Configurando a proteção da branch `main`

Isso é feito no GitHub, uma vez, e **não** dá para versionar no repositório.
Faça logo depois de criar o repositório remoto.

**Settings → Branches → Add branch protection rule**

| Configuração | Valor |
|---|---|
| Branch name pattern | `main` |
| Require a pull request before merging | ✅ |
| Require status checks to pass before merging | ✅ |
| ↳ Status checks obrigatórios | `Testes unitários (native)`, `Cobertura (100% linhas)`, `Política de testes`, `CHANGELOG atualizado`, `Build do firmware (esp32dev)`, `Conventional Commits` |
| Require branches to be up to date before merging | ✅ |
| Do not allow bypassing the above settings | ✅ |

Ou pela CLI:

```bash
gh api -X PUT repos/:owner/:repo/branches/main/protection \
  --input - <<'JSON'
{
  "required_status_checks": {
    "strict": true,
    "contexts": [
      "Testes unitários (native)",
      "Cobertura (100% linhas)",
      "Política de testes",
      "CHANGELOG atualizado",
      "Build do firmware (esp32dev)",
      "Conventional Commits"
    ]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": null,
  "restrictions": null
}
JSON
```

> Os nomes em `contexts` têm que bater **exatamente** com o campo `name:` de
> cada job em `.github/workflows/ci.yml`.

---

## Adicionando um módulo ou funcionalidade

Siga a mesma estrutura que já existe — ela não é decoração, é o que mantém os
testes rodando no PC:

1. **Interface** (a "porta") em `lib/<modulo>/include/<modulo>/`
   — sem `Arduino.h`, e com um comentário explicando por que o módulo existe.
2. **Lógica** em `lib/<modulo>/src/` — pura, testável.
3. **Adaptador de hardware** em `src/hal/` — burro, sem lógica.
4. **Dublê de teste** em `test/helpers/` — header-only.
5. **Testes** em `test/test_<coisa>/` — com **invariantes**, não só exemplos.
   Isso não é opcional: o CI exige 100% de cobertura de linhas em `lib/`.
   Ver [docs/TESTING.md](docs/TESTING.md).

Se você se pegar querendo incluir `Arduino.h` em `lib/`, pare: o que você
precisa é de uma porta nova.

---

## O CHANGELOG

**Toda mudança de código ou de infra entra no `CHANGELOG.md`, no mesmo PR.**
Isso é cobrado pelo job `CHANGELOG atualizado`.

Enquanto a mudança não foi lançada, ela vive na seção `[Não lançado]`:

```markdown
## [Não lançado]

### Adicionado
- suporte a leitura do PID 0x0C (rotação do motor)

### Corrigido
- estouro de inteiro no backoff exponencial
```

Seções do [Keep a Changelog](https://keepachangelog.com/pt-BR/1.1.0/):
`Adicionado`, `Modificado`, `Descontinuado`, `Removido`, `Corrigido`,
`Segurança`.

Escreva para **quem vai ler daqui a seis meses**, não para o `git log`:

```markdown
# ❌ repete o commit
- altera obd_client.cpp

# ✅ diz o que mudou para quem usa
- Corrigido: resposta atrasada de um PID anterior não é mais exibida como a
  medida atual (o parser agora confere o eco de modo e PID)
```

**Dispensa:** se a mudança realmente não merece registro, adicione a label
`sem-changelog` no PR. Como as outras dispensas do projeto, ela fica visível
na revisão.

## Publicando uma versão

```bash
# 1. Atualize a versão (fonte única da verdade)
$EDITOR lib/kanri_core/include/kanri_core/version.h

# 2. Mova o que estava em [Não lançado] para uma seção da nova versão
$EDITOR CHANGELOG.md

# 3. Commit, PR, merge na main

# 4. Só então a tag, já na main
git switch main && git pull
git tag -a v0.2.0 -m "v0.2.0 — conexão Bluetooth e leitura de PIDs"
git push origin v0.2.0
```

As três coisas — `version.h`, `CHANGELOG.md` e a tag — **andam juntas**. Se
divergirem, ninguém sabe mais o que está rodando no aparelho.
