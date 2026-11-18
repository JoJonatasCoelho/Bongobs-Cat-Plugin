# Bongobs Cat — plugin para OBS

[![Linux build](https://github.com/JoJonatasCoelho/Bongobs-Cat-Plugin/actions/workflows/linux-build.yml/badge.svg)](https://github.com/JoJonatasCoelho/Bongobs-Cat-Plugin/actions/workflows/linux-build.yml)

Overlay de **bongo cat** para o OBS Studio. O gato se move conforme o que você
digita e clica no computador: as patas acendem conforme as teclas pressionadas,
os olhos seguem o mouse e o bichinho reage aos cliques.

O plugin é implementado como uma **fonte do OBS** e usa o **Live2D Cubism Native
Framework** com renderização em **OpenGL**, processado numa thread própria e
enviado ao OBS como textura.

**Licença:** GPL v2 (veja [`LICENSE`](LICENSE)).

---

## Suporte

* **Windows:** OBS Studio 25.0.0 ou superior.
* **Linux:** OBS Studio 28 ou superior — validado no **OBS 33.0.1** (Flatpak)
  em sessão Wayland e X11.

## Windows

### Instalar a versão pré-compilada

1. Baixe o [Bongo.Cat.zip](https://github.com/a1928370421/Bongobs-Cat-Plugin/releases/download/0.1.1/Bango.Cat.zip)
2. Descompacte na pasta raiz do OBS.

### Compilar no Windows

Requer Visual Studio 2015 ou superior e o SDK do Live2D Cubism para Windows.
O CMake usa `find_package(libobs)` (headers/libobs do OBS instalado ou
`-DCMAKE_PREFIX_PATH=<prefixo>`), monta o Cubism Core do `CubismSdk/Core/lib/windows/`
e gera `bongobs-cat.dll`.

---

## Linux

### Dependências de build

```bash
# Ubuntu / Debian
sudo apt install build-essential cmake git \
                 libgl1-mesa-dev libegl1-mesa-dev libx11-dev
```

> Não é necessário `libobs-dev`: o build usa os **headers reais do libobs**
> (baixados do fonte do obs-studio) e linka contra um stub de importação com o
> SONAME correto (`libobs.so.N`), de modo que os símbolos são resolvidos pelo
> próprio OBS em tempo de execução — exatamente como um plugin compilado com
> `libobs-dev`.

O SDK do Live2D Cubism já vem no repositório
(`CubismSdk/Core/lib/linux/x86_64/libLive2DCubismCore.a`).

### Compilar

**Sem `libobs-dev` (recomendado):**

```bash
packaging/build-with-obs-headers.sh --prefix ~/.config/obs-studio
```

O script baixa os headers do obs-studio (`33.0.1` por padrão) e do `simde`
para `~/.cache/bongobs-cat` (reaproveitados nas próximas vezes) e compila o
plugin. Variáveis: `OBS_TAG`, `SIMDE_TAG`, `CACHE_DIR`, `BUILD_DIR`, `JOBS`.

**Com o `libobs` de desenvolvimento instalado:**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
cmake --install build --prefix ~/.config/obs-studio
```

### Instalação

O plugin precisa ficar em `plugins/bongobs-cat/`, com o `.so` em `bin/64bit/`
e os dados em `data/`:

```
<raiz>/plugins/bongobs-cat/
├── bin/64bit/bongobs-cat.so
└── data/
    ├── locale/{en-US,pt-BR,ru-RU,zh-CN}.ini
    └── Bango Cat/...          (modelos, modos, imagens, configurações)
```

#### OBS instalado via Flatpak

```bash
cmake --install build --prefix ~/.var/app/com.obsproject.Studio/config/obs-studio
```

Ou, com o bundle gerado pelo `make-release.sh`:

```bash
./install.sh            # detecta automaticamente o Flatpak
```

Reinicie o OBS depois de instalar.

#### OBS nativo (por usuário)

```bash
cmake --install build --prefix ~/.config/obs-studio
```

Ou `./install.sh` (detecta `~/.config/obs-studio` e, no OBS 31+, também
`${XDG_DATA_HOME:-~/.local/share}/obs-studio`).

Depois: adicione a fonte **bongobs cat** no painel de fontes do OBS.

### Captura de teclado e mouse (Linux)

No Linux a captura global é feita lendo os dispositivos de entrada do kernel
(`/dev/input/event*`) — funciona em sessões **X11** e **Wayland**, sem depender
do compositor.

**Permissões:** os devices pertencem a `root:input`, então é preciso um dos
dois:

*Adicionar o usuário ao grupo `input`* (vale para a sessão inteira):

```bash
sudo usermod -aG input "$USER"    # depois: sair e entrar na sessão
```

*Ou instalar a regra udev* (recomendado: dá acesso apenas ao usuário da
sessão ativa, sem mudar grupos):

```bash
sudo cp packaging/udev/99-bongobs-cat-input.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Depois saia e entre na sessão (ou reinicie).

Sem permissão o plugin avisa no log do OBS e continua funcionando — apenas a
captura de teclado/mouse fica desativada.

**Mouse em Wayland / Flatpak:** não existe posição absoluta global do cursor
nesses ambientes. Nesses casos o plugin **entra automaticamente no modo
relativo** (não é preciso ativar *Relative Mouse Movement*). Em sessões X11 a
posição absoluta é usada normalmente.

**Conferindo no log:** o plugin lista os dispositivos detectados
(`ajuda → logs` ou `~/.var/app/com.obsproject.Studio/config/obs-studio/logs/`):

```
[bongobs-cat] evdev: /dev/input/event19 (YJS MicroChip Mechanical Keyboard) -> teclado=sim mouse=nao
[bongobs-cat] evdev: 8 dispositivo(s) aberto(s) - 5 teclado(s), 3 mouse(s).
[bongobs-cat] EGL: contexto offscreen criado (1280x768, EGL 1.5).
```

### Empacotamento e release

```bash
packaging/make-release.sh
```

Compila o plugin e gera:

```
dist/bongobs-cat-linux-<versão>/          (bin/64bit + data + install.sh)
dist/bongobs-cat-linux-<versão>.tar.gz
```

O `install.sh` gerado detecta o destino (Flatpak, usuário ou sistema) e aceita
`--dest <dir>`, `--dry-run` e `--system` (requer root). Detalhes em
[`packaging/README.md`](packaging/README.md).

### Integração contínua

O workflow [`.github/workflows/linux-build.yml`](.github/workflows/linux-build.yml)
roda a cada push/PR:

* **compile-check** — `g++ -fsyntax-only` em todas as fontes contra os headers
  reais do libobs (uns 9 segundos);
* **build** — compila, linka e publica o bundle `.tar.gz` como artefato.

---

## Como funciona por dentro (Linux)

* **Contexto OpenGL isolado:** o plugin cria um contexto **EGL offscreen**
  (surfaceless + pbuffer) numa thread própria, com o GLEW compilado em modo
  `GLEW_EGL`. Por isso não depende de janela, de X11 nem de Wayland — e
  funciona dentro do sandbox do Flatpak. A thread do OBS nunca executa GL cru:
  ela apenas copia o frame publicado para uma `gs_texture` cacheada, o que é
  essencial porque no Linux o OBS usa EGL (`gs_enter_context()` não religa o
  contexto a cada frame).
* **Windows:** o caminho continua sendo o GLFW (janela oculta) + hooks de
  baixo nível (`WH_KEYBOARD_LL`/`WH_MOUSE_LL`) + Raw Input.
* **Renderização:** textura de 1280×768 enviada ao OBS a cada frame — mesmo
  comportamento da versão original do Windows.

## Limitações conhecidas

* **Flatpak:** o OBS em Flatpak enxerga `/dev/input` (o app tem `devices=all`),
  e a permissão do usuário do host é herdada — ou seja, se o grupo `input` ou a
  regra udev estiverem ativos, a captura funciona lá dentro. O que **não**
  existe no sandbox é a posição absoluta do cursor (sem servidor X), então o
  modo relativo é usado automaticamente. A instalação de plugins no Flatpak não
  é oficialmente suportada pelo OBS — o caminho
  `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/` é o que
  funciona hoje.
* **Wayland:** sem posição absoluta do cursor (limitação do próprio protocolo).
* O plugin não altera a configuração de áudio/vídeo do OBS e não depende de
  rede.

## Bongo cat

![](https://github.com/JoJonatasCoelho/Bongobs-Cat-Plugin/blob/master/Resources/Bango%20Cat/ezgif-2-81825e3faab3.gif)
