# Contribuindo com o Kanri

Antes de tudo: leia **[docs/SAFETY.md](docs/SAFETY.md)** e
**[CLAUDE.md](CLAUDE.md)**.

---

## Preparando o ambiente

```bash
git clone <url> kanri
cd kanri

pip install platformio          # ou instale a extensão PlatformIO no VS Code

# Ativa o hook que valida a mensagem de commit localmente.
# Faça isso uma vez por clone — o Git não versiona a configuração de hooks.
git config core.hooksPath .githooks

pio test -e native              # confirma que tudo funciona: 98 casos verdes
```

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
| ↳ Status checks obrigatórios | `Testes unitários (native)`, `Build do firmware (esp32dev)`, `Conventional Commits` |
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

Se você se pegar querendo incluir `Arduino.h` em `lib/`, pare: o que você
precisa é de uma porta nova.

---

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
