
# Bongobs Cat OBS Plugin
 Obs Live2d 插件
 Bongo cat overlay for OBS plugin. This plugin is based on the built in Live2d CubismNativeFrameWork & Opengl. 
# 支持
* 支持 OBS Studio version : 25.0.0+
# Support
* Support OBS Studio version** : 25.0.0+
# 如何使用
* 下载 [Bongo.Cat.zip](https://github.com/a1928370421/Bongobs-Cat-Plugin/releases/download/0.1.1/Bango.Cat.zip)
* 解压缩文件到OBS根目录
# How to use
* Download [Bongo.Cat.zip](https://github.com/a1928370421/Bongobs-Cat-Plugin/releases/download/0.1.1/Bango.Cat.zip)
* Unzip file to OBS root direction
# Bongo cat
![](https://github.com/a1928370421/Bongobs-Cat-Plugin/blob/master/Resources/Bango%20Cat/ezgif-2-81825e3faab3.gif)

---

# Linux (novo)

O plugin agora compila e roda em **Linux** (OBS Studio 28+), usando **evdev** para captura
global de teclado/mouse e um contexto OpenGL isolado em thread dedicada (compatível com o
backend OpenGL/EGL do OBS no Linux).

## Dependências de build

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake git \
                 libobs-dev \
                 libgl1-mesa-dev libglu1-mesa-dev \
                 xorg-dev libx11-dev
```

* `libobs-dev` vem do repositório oficial do OBS. Se o seu distro não tiver o pacote, compile
  o `obs-studio` a partir do fonte e aponte o CMake para o prefixo dele
  (`-DCMAKE_PREFIX_PATH=/caminho/do/obs-studio/install`).
* O SDK do Live2D Cubism já está incluído no repositório
  (`CubismSdk/Core/lib/linux/x86_64/libLive2DCubismCore.a`).

## Compilando

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## Instalando (sem root, layout de usuário do OBS)

```bash
cmake --install build --prefix ~/.config/obs-studio
```

Isso cria:

```
~/.config/obs-studio/plugins/bongobs-cat/
├── bin/64bit/bongobs-cat.so
└── data/
    ├── locale/{en-US,ru-RU,zh-CN}.ini
    └── Bango Cat/...        (modelos, modos, imagens, configs)
```

Reinicie o OBS e adicione a fonte **Bongo Cat** no painel de fontes.

> Se preferir o layout novo do OBS 31+ (`$XDG_DATA_HOME/obs-studio/plugins/bongobs-cat/`),
> instale com `--prefix ~/.local/share` — o conteúdo é o mesmo, só muda a raiz.

### OBS instalado via Flatpak

O Flatpak do OBS (`com.obsproject.Studio`) usa o seu diretório de configuração, então o
prefixo de instalação é:

```bash
cmake --install build --prefix ~/.var/app/com.obsproject.Studio/config/obs-studio
```

Isso cria `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/bongobs-cat/` com o
mesmo layout (`bin/64bit/bongobs-cat.so` + `data/`), que é o caminho que o OBS do Flatpak
procura. Reinicie o OBS depois de instalar.

> O `.so` precisa casar com a versão do `libobs` do seu OBS (`libobs.so.30` para o OBS 30,
> `libobs.so.31` para o 31, ...). Se o OBS reclamar de `libobs.so.N` ausente, o plugin foi
> compilado contra outra versão — recompile com o `libobs` da mesma versão do OBS instalado.

## Captura de input (teclado/mouse)

No Linux a captura global é feita lendo os dispositivos de entrada do kernel (`/dev/input/event*`).

* Em sessões **X11** funciona automaticamente.
* Em sessões **Wayland** também funciona (evdev não depende do compositor), mas a **posição
  absoluta do cursor não existe** por padrão no Wayland. Nesse caso o plugin **usa o modo
  relativo automaticamente** (você não precisa ativar *Relative Mouse Movement*).
* É preciso permissão de leitura em `/dev/input`. Em sistemas com `systemd`/`logind` o usuário da
  sessão já tem acesso via ACL; caso contrário:
  ```bash
  sudo usermod -aG input "$USER"   # requer novo login
  ```
  Se a permissão faltar, o plugin avisa no log do OBS e continua funcionando (apenas a captura
  de teclado/mouse fica desativada).

## Observações / limitações

* **Renderização**: no Linux o plugin cria um contexto **EGL offscreen** (surfaceless + pbuffer),
  sem janela e sem depender de X11/Wayland — por isso ele funciona tanto no OBS do Flatpak
  quanto em sessões Wayland. O render acontece numa thread dedicada; a thread do OBS nunca
  toca no contexto do OBS (que no Linux é EGL).
* **Flatpak**: o OBS em Flatpak tem `devices=all`, então `/dev/input` aparece dentro do
  sandbox e a captura de teclado/mouse funciona **se o usuário do host tiver permissão**
  (ACL do logind ou grupo `input`). A posição absoluta do cursor não existe no sandbox
  (não há servidor X), então o modo relativo é usado automaticamente.
* O plugin desenha em uma textura de 1280x768 e faz upload para o OBS a cada frame (mesmo
  comportamento da versão Windows).

## Windows

O caminho Windows continua suportado (hooks de baixo nível `WH_KEYBOARD_LL`/`WH_MOUSE_LL` +
Raw Input). Para compilar no Windows é necessário Visual Studio 2015+ e o SDK do Live2D Cubism
para Windows; a build standalone usa `find_package(libobs)` da mesma forma.
