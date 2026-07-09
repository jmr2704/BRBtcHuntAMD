#!/usr/bin/env bash
# ──────────────────────────────────────────────────────────────────────────────
# setup-linux.sh — Automated build environment setup for BRBtcHuntAMD (Linux)
# ──────────────────────────────────────────────────────────────────────────────
# Idiomas: Português (pt-BR) / English (en)
# Uso: sudo ./scripts/setup-linux.sh
# ──────────────────────────────────────────────────────────────────────────────

set -euo pipefail

# ── Config ──────────────────────────────────────────────────────────────────
LANG_CHOICE="${LANG_CHOICE:-pt}"  # pt | en
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# ── Logging helpers ─────────────────────────────────────────────────────────
msg() {
    local pt_msg="$1"; local en_msg="$2"
    if [ "$LANG_CHOICE" = "pt" ]; then echo -e "🐊 $pt_msg"; else echo -e "🐊 $en_msg"; fi
}
ok() {
    local pt_msg="$1"; local en_msg="$2"
    if [ "$LANG_CHOICE" = "pt" ]; then echo -e "✅ $pt_msg"; else echo -e "✅ $en_msg"; fi
}
err() {
    local pt_msg="$1"; local en_msg="$2"
    if [ "$LANG_CHOICE" = "pt" ]; then echo -e "❌ $pt_msg" >&2; else echo -e "❌ $en_msg" >&2; fi
}
warn() {
    local pt_msg="$1"; local en_msg="$2"
    if [ "$LANG_CHOICE" = "pt" ]; then echo -e "⚠️  $pt_msg"; else echo -e "⚠️  $en_msg"; fi
}
die() {
    err "$1" "$2"
    exit 1
}

# ── Banner ──────────────────────────────────────────────────────────────────
echo ""
msg "═══ Instalador automático — BRBtcHuntAMD (Linux) ═══" \
     "═══ Automated Setup — BRBtcHuntAMD (Linux) ═══"
msg "Repo: $REPO_DIR" \
     "Repo: $REPO_DIR"
echo ""

# ──────────────────────────────────────────────────────────────────────────────
# Etapa 1: Validação de root
# ──────────────────────────────────────────────────────────────────────────────
echo "──────────────────────────────────────────────────────────────────────────"
msg "Etapa 1/6 — Verificando permissão..." \
     "Step 1/6 — Checking permissions..."

if [ "$EUID" -ne 0 ]; then
    die "Este script precisa ser executado como ROOT (sudo).
  Comando correto: sudo ./scripts/setup-linux.sh
  Motivo: instalação de pacotes do sistema, drivers ROCm e configuração de GPU requerem privilégios de administrador." \
        "This script must be run as ROOT (sudo).
  Correct command: sudo ./scripts/setup-linux.sh
  Reason: system package installation, ROCm drivers, and GPU configuration require administrator privileges."
fi

ok "Permissão root confirmada." \
   "Root permission confirmed."
echo ""

# ──────────────────────────────────────────────────────────────────────────────
# Etapa 2: Detecção da distribuição Linux
# ──────────────────────────────────────────────────────────────────────────────
echo "──────────────────────────────────────────────────────────────────────────"
msg "Etapa 2/6 — Detectando distribuição Linux..." \
     "Step 2/6 — Detecting Linux distribution..."

if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO="$ID"
    DISTRO_VER="$VERSION_ID"
    DISTRO_NAME="$NAME"
else
    die "Não foi possível detectar sua distribuição Linux (/etc/os-release não encontrado).
  Distribuições suportadas: Ubuntu 22.04+, Debian 12+, Arch Linux, Fedora 38+
  Para adicionar suporte a outra distro, contribua em: https://github.com/jacazul-ai/BitcoinToolsAMD" \
        "Could not detect your Linux distribution (/etc/os-release not found).
  Supported distributions: Ubuntu 22.04+, Debian 12+, Arch Linux, Fedora 38+
  To add support for another distro, contribute at: https://github.com/jacazul-ai/BitcoinToolsAMD"
fi

msg "Distribuição detectada: $DISTRO_NAME $DISTRO_VER" \
    "Distribution detected: $DISTRO_NAME $DISTRO_VER"

# Verificar suporte
case "$DISTRO" in
    ubuntu|debian|arch|manjaro|fedora)
        ok "Distribuição suportada!" \
           "Distribution supported!"
        ;;
    *)
        die "Distribuição '$DISTRO_NAME' não é oficialmente suportada.
  Suportadas: Ubuntu 22.04+, Debian 12+, Arch Linux, Fedora 38+
  Você pode tentar manualmente: veja o arquivo docs/BUILD.md para instruções de compilação manual." \
            "Distribution '$DISTRO_NAME' is not officially supported.
  Supported: Ubuntu 22.04+, Debian 12+, Arch Linux, Fedora 38+
  You can try manually: see docs/BUILD.md for manual build instructions."
        ;;
esac
echo ""

# ──────────────────────────────────────────────────────────────────────────────
# Etapa 3: Instalação de dependências do sistema
# ──────────────────────────────────────────────────────────────────────────────
echo "──────────────────────────────────────────────────────────────────────────"
msg "Etapa 3/6 — Instalando dependências do sistema..." \
     "Step 3/6 — Installing system dependencies..."

install_packages() {
    case "$DISTRO" in
        ubuntu|debian)
            msg "Usando apt (Ubuntu/Debian)..." \
                "Using apt (Ubuntu/Debian)..."
            apt update -qq || warn "Falha ao atualizar apt. Continuando..." \
                                       "apt update failed. Continuing..."
            apt install -y build-essential cmake git wget curl gpg software-properties-common \
                || die "Falha ao instalar pacotes base via apt.
  Tente manualmente: sudo apt update && sudo apt install -y build-essential cmake git wget curl
  Se o erro persistir, verifique sua conexão com a internet e os repositórios configurados em /etc/apt/sources.list" \
                       "Failed to install base packages via apt.
  Try manually: sudo apt update && sudo apt install -y build-essential cmake git wget curl
  If the error persists, check your internet connection and repositories in /etc/apt/sources.list"
            ;;
        arch|manjaro)
            msg "Usando pacman (Arch Linux)..." \
                "Using pacman (Arch Linux)..."
            pacman -Sy --noconfirm base-devel cmake git wget curl \
                || die "Falha ao instalar pacotes base via pacman.
  Tente manualmente: sudo pacman -Sy --noconfirm base-devel cmake git wget curl
  Se o erro persistir, verifique sua conexão e a configuração do pacman." \
                       "Failed to install base packages via pacman.
  Try manually: sudo pacman -Sy --noconfirm base-devel cmake git wget curl
  If the error persists, check your internet connection and pacman configuration."
            ;;
        fedora)
            msg "Usando dnf (Fedora)..." \
                "Using dnf (Fedora)..."
            dnf groupinstall -y "Development Tools" || true
            dnf install -y cmake git wget curl \
                || die "Falha ao instalar pacotes base via dnf.
  Tente manualmente: sudo dnf install -y cmake git wget curl gcc-c++
  Se o erro persistir, verifique sua conexão e os repositórios configurados." \
                       "Failed to install base packages via dnf.
  Try manually: sudo dnf install -y cmake git wget curl gcc-c++
  If the error persists, check your internet connection and repository configuration."
            ;;
    esac
}

install_packages
ok "Dependências do sistema instaladas." \
   "System dependencies installed."
echo ""

# ──────────────────────────────────────────────────────────────────────────────
# Etapa 4: Instalação do ROCm / HIP SDK
# ──────────────────────────────────────────────────────────────────────────────
echo "──────────────────────────────────────────────────────────────────────────"
msg "Etapa 4/6 — Instalando ROCm HIP SDK..." \
     "Step 4/6 — Installing ROCm HIP SDK..."

check_hip() {
    if command -v hipcc &>/dev/null; then
        msg "hipcc encontrado: $(hipcc --version 2>/dev/null | head -1)" \
            "hipcc found: $(hipcc --version 2>/dev/null | head -1)"
        return 0
    fi
    # Verificar em localizações comuns
    for candidate in /opt/rocm/bin/hipcc /opt/rocm-*/bin/hipcc; do
        [ -x "$candidate" ] && {
            msg "hipcc encontrado em: $candidate" \
                "hipcc found at: $candidate"
            export PATH="$PATH:$(dirname "$candidate")"
            return 0
        }
    done
    return 1
}

if check_hip; then
    ok "ROCm HIP SDK já está instalado. Pulando instalação." \
       "ROCm HIP SDK is already installed. Skipping installation."
else
    msg "ROCm HIP SDK não encontrado. Iniciando instalação automática..." \
        "ROCm HIP SDK not found. Starting automatic installation..."

    case "$DISTRO" in
        ubuntu|debian)
            msg "Configurando repositório ROCm AMD oficial..." \
                "Setting up official AMD ROCm repository..."

            # Determinar versão do Ubuntu para o repo ROCm
            ROCM_REPO="https://repo.radeon.com/rocm/apt/latest"
            
            # Instalar chave GPG
            mkdir -p /etc/apt/keyrings || true
            wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key | gpg --dearmor -o /etc/apt/keyrings/rocm.gpg 2>/dev/null || {
                die "Falha ao baixar a chave GPG do repositório ROCm.
  Verifique sua conexão com a internet ou tente manualmente:
    wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key | sudo gpg --dearmor -o /etc/apt/keyrings/rocm.gpg
  Se o site repo.radeon.com estiver bloqueado, use um proxy ou VPN." \
                    "Failed to download ROCm repository GPG key.
  Check your internet connection or try manually:
    wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key | sudo gpg --dearmor -o /etc/apt/keyrings/rocm.gpg
  If repo.radeon.com is blocked, use a proxy or VPN."
            }

            # Adicionar repositório
            echo "deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] $ROCM_REPO ubuntu main" > /etc/apt/sources.list.d/rocm.list

            # Adicionar usuário ao grupo render/video (necessário para acesso à GPU)
            usermod -a -G render,video "$SUDO_USER" 2>/dev/null || true

            apt update -qq || warn "Falha ao atualizar com repositório ROCm. Verifique sua conexão." \
                                       "Failed to update with ROCm repository. Check your connection."

            msg "Instalando rocm-hip-sdk (pacote completo do ROCm HIP)..." \
                "Installing rocm-hip-sdk (full ROCm HIP package)..."
            
            # Tenta instalar o SDK completo; se falhar, tenta apenas o runtime mínimo
            apt install -y rocm-hip-sdk 2>/dev/null || {
                warn "Pacote rocm-hip-sdk não disponível. Tentando rocm-hip-runtime..." \
                     "Package rocm-hip-sdk not available. Trying rocm-hip-runtime..."
                apt install -y rocm-hip-runtime 2>/dev/null || {
                    die "Falha ao instalar ROCm HIP.
  Tente manualmente seguindo o guia oficial da AMD:
    https://rocm.docs.amd.com/en/latest/deploy/linux/install.html
  
  Para Ubuntu:
    wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key | sudo gpg --dearmor -o /etc/apt/keyrings/rocm.gpg
    echo 'deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] https://repo.radeon.com/rocm/apt/latest ubuntu main' | sudo tee /etc/apt/sources.list.d/rocm.list
    sudo apt update && sudo apt install -y rocm-hip-sdk
  
  Após instalar, adicione seu usuário aos grupos render e video:
    sudo usermod -a -G render,video \$USER
  E faça logout/login para aplicar." \
                        "Failed to install ROCm HIP.
  Try manually following AMD's official guide:
    https://rocm.docs.amd.com/en/latest/deploy/linux/install.html
  
  For Ubuntu:
    wget -q -O - https://repo.radeon.com/rocm/rocm.gpg.key | sudo gpg --dearmor -o /etc/apt/keyrings/rocm.gpg
    echo 'deb [arch=amd64 signed-by=/etc/apt/keyrings/rocm.gpg] https://repo.radeon.com/rocm/apt/latest ubuntu main' | sudo tee /etc/apt/sources.list.d/rocm.list
    sudo apt update && sudo apt install -y rocm-hip-sdk
  
  After installation, add your user to the render and video groups:
    sudo usermod -a -G render,video \$USER
  Then log out and back in to apply."
                }
            }
            ;;

        arch|manjaro)
            msg "ROCm disponível via AUR no Arch Linux. Instalando via yay/paru..." \
                "ROCm is available via AUR on Arch Linux. Installing via yay/paru..."
            
            if command -v yay &>/dev/null; then
                sudo -u "$SUDO_USER" yay -S --noconfirm rocm-hip-sdk 2>/dev/null || {
                    die "Falha ao instalar rocm-hip-sdk via yay.
  Tente manualmente:
    yay -S rocm-hip-sdk
  Ou siga o guia Arch Wiki:
    https://wiki.archlinux.org/title/ROCm" \
                        "Failed to install rocm-hip-sdk via yay.
  Try manually:
    yay -S rocm-hip-sdk
  Or follow the Arch Wiki:
    https://wiki.archlinux.org/title/ROCm"
                }
            elif command -v paru &>/dev/null; then
                sudo -u "$SUDO_USER" paru -S --noconfirm rocm-hip-sdk 2>/dev/null || {
                    die "Falha ao instalar rocm-hip-sdk via paru.
  Tente manualmente:
    paru -S rocm-hip-sdk" \
                        "Failed to install rocm-hip-sdk via paru.
  Try manually:
    paru -S rocm-hip-sdk"
                }
            else
                die "AUR helper (yay/paru) não encontrado.
  Instale primeiro o yay:
    sudo pacman -S --needed git base-devel
    git clone https://aur.archlinux.org/yay-bin.git
    cd yay-bin && makepkg -si
  Depois execute este script novamente.
  
  Ou instale o ROCm manualmente via:
    yay -S rocm-hip-sdk" \
                    "AUR helper (yay/paru) not found.
  Install yay first:
    sudo pacman -S --needed git base-devel
    git clone https://aur.archlinux.org/yay-bin.git
    cd yay-bin && makepkg -si
  Then run this script again.
  
  Or install ROCm manually via:
    yay -S rocm-hip-sdk"
            fi
            ;;

        fedora)
            msg "Configurando repositório ROCm para Fedora..." \
                "Setting up ROCm repository for Fedora..."
            
            # Fedora usa o repositório ROCm via dnf
            dnf install -y 'https://repo.radeon.com/rocm/install/latest/rpm/amdgpu-install-1.0.0-1.noarch.rpm' 2>/dev/null || {
                die "Falha ao baixar o instalador AMDGPU para Fedora.
  Tente manualmente:
    sudo dnf install -y 'https://repo.radeon.com/rocm/install/latest/rpm/amdgpu-install-1.0.0-1.noarch.rpm'
    sudo amdgpu-install -y --usecase=rocm
  
  Veja o guia oficial: https://rocm.docs.amd.com/en/latest/deploy/linux/install.html" \
                    "Failed to download AMDGPU installer for Fedora.
  Try manually:
    sudo dnf install -y 'https://repo.radeon.com/rocm/install/latest/rpm/amdgpu-install-1.0.0-1.noarch.rpm'
    sudo amdgpu-install -y --usecase=rocm
  
  See official guide: https://rocm.docs.amd.com/en/latest/deploy/linux/install.html"
            }
            amdgpu-install -y --usecase=rocm 2>/dev/null || {
                die "Falha ao executar amdgpu-install.
  Execute manualmente:
    sudo amdgpu-install -y --usecase=rocm" \
                    "Failed to run amdgpu-install.
  Run manually:
    sudo amdgpu-install -y --usecase=rocm"
            }
            ;;
    esac

    # Verificar instalação
    if check_hip; then
        ok "ROCm HIP SDK instalado com sucesso!" \
           "ROCm HIP SDK installed successfully!"
    else
        die "ROCm HIP SDK foi instalado mas o comando 'hipcc' não foi encontrado.
  Tente recarregar o PATH: export PATH=/opt/rocm/bin:\$PATH
  Ou faça logout/login e execute este script novamente.
  
  Se ainda assim não funcionar, verifique a instalação manualmente:
    ls -la /opt/rocm/bin/hipcc
    /opt/rocm/bin/hipcc --version" \
            "ROCm HIP SDK was installed but 'hipcc' was not found.
  Try reloading PATH: export PATH=/opt/rocm/bin:\$PATH
  Or log out and back in, then run this script again.
  
  If it still doesn't work, check the installation manually:
    ls -la /opt/rocm/bin/hipcc
    /opt/rocm/bin/hipcc --version"
    fi
fi
echo ""

# ──────────────────────────────────────────────────────────────────────────────
# Etapa 5: Compilação do BRBtcHuntAMD
# ──────────────────────────────────────────────────────────────────────────────
echo "──────────────────────────────────────────────────────────────────────────"
msg "Etapa 5/6 — Compilando BRBtcHuntAMD..." \
     "Step 5/6 — Building BRBtcHuntAMD..."

cd "$REPO_DIR"

# Verificar se o Makefile existe
if [ ! -f Makefile ]; then
    die "Makefile não encontrado em $REPO_DIR.
  Certifique-se de estar no diretório correto do repositório.
  Repositório: https://github.com/jacazul-ai/BitcoinToolsAMD" \
        "Makefile not found in $REPO_DIR.
  Make sure you're in the correct repository directory.
  Repository: https://github.com/jacazul-ai/BitcoinToolsAMD"
fi

# Executar make
msg "Executando: make clean && make" \
    "Running: make clean && make"
make clean 2>/dev/null || true

if make -j"$(nproc)" 2>&1; then
    ok "Compilação concluída com sucesso!" \
       "Build completed successfully!"
else
    # Se falhou, tentar com arquitetura única (fallback)
    warn "Falha ao compilar com todas as arquiteturas GPU. Tentando apenas gfx1032..." \
         "Build failed with all GPU architectures. Trying only gfx1032..."
    
    if make -j"$(nproc)" GPU_ARCHS="gfx1032" 2>&1; then
        ok "Compilação concluída com sucesso (apenas RX 6600)!" \
           "Build completed successfully (RX 6600 only)!"
    else
        die "Falha na compilação.
  Verifique se o ROCm está corretamente instalado:
    hipcc --version
    rocminfo | head -20
  
  Erros comuns:
  1. 'hipcc not found': ROCm não está no PATH. Adicione: export PATH=/opt/rocm/bin:\$PATH
  2. 'cannot find -lamdhip64': Biblioteca HIP não encontrada. Verifique: ls /opt/rocm/lib/libamdhip64*
  3. Erros de GPU architecture: Sua GPU pode não ser compatível. Execute: rocminfo | grep gfx
  
  Para ajuda: https://github.com/jacazul-ai/BitcoinToolsAMD/issues" \
            "Build failed.
  Verify that ROCm is properly installed:
    hipcc --version
    rocminfo | head -20
  
  Common errors:
  1. 'hipcc not found': ROCm not in PATH. Fix: export PATH=/opt/rocm/bin:\$PATH
  2. 'cannot find -lamdhip64': HIP library not found. Check: ls /opt/rocm/lib/libamdhip64*
  3. GPU architecture errors: Your GPU may not be supported. Run: rocminfo | grep gfx
  
  For help: https://github.com/jacazul-ai/BitcoinToolsAMD/issues"
    fi
fi
echo ""

# ──────────────────────────────────────────────────────────────────────────────
# Etapa 6: Validação do binário
# ──────────────────────────────────────────────────────────────────────────────
echo "──────────────────────────────────────────────────────────────────────────"
msg "Etapa 6/6 — Validando binário..." \
     "Step 6/6 — Validating binary..."

BINARY="./BRBtcHuntAMD"
if [ ! -f "$BINARY" ]; then
    die "Binário não encontrado: $BINARY
  Algo deu errado na compilação.
  Verifique os logs acima e tente compilar manualmente:
    cd $REPO_DIR && make clean && make GPU_ARCHS=gfx1032" \
        "Binary not found: $BINARY
  Something went wrong during compilation.
  Check the logs above and try building manually:
    cd $REPO_DIR && make clean && make GPU_ARCHS=gfx1032"
fi

if "$BINARY" --help &>/dev/null; then
    ok "Binário compilado e funcionando!" \
       "Binary compiled and working!"
    msg "Comando de exemplo:
  $BINARY --range 2000000000:3FFFFFFFFF --address 1PWo6GdxJ4CmB6LuDCm9oXxwuunEHNEMk" \
        "Example command:
  $BINARY --range 2000000000:3FFFFFFFFF --address 1PWo6GdxJ4CmB6LuDCm9oXxwuunEHNEMk"
else
    die "Binário não executou corretamente.
  Tente executar manualmente:
    $BINARY --help
  Se houver erro de 'cannot open shared object file', faltam bibliotecas ROCm.
  Execute: export LD_LIBRARY_PATH=/opt/rocm/lib:\$LD_LIBRARY_PATH
  E adicione ao seu ~/.bashrc" \
        "Binary did not execute correctly.
  Try running manually:
    $BINARY --help
  If there's a 'cannot open shared object file' error, ROCm libraries are missing.
  Run: export LD_LIBRARY_PATH=/opt/rocm/lib:\$LD_LIBRARY_PATH
  And add it to your ~/.bashrc"
fi

# ──────────────────────────────────────────────────────────────────────────────
# Finalização
# ──────────────────────────────────────────────────────────────────────────────
echo "──────────────────────────────────────────────────────────────────────────"
echo ""
ok "Setup concluído com sucesso! 🎯" \
   "Setup completed successfully! 🎯"
msg "
  Resumo / Summary:
  ───────────────────────────────────
  ✅ Root / Admin: OK
  ✅ Distribuição: $DISTRO_NAME $DISTRO_VER
  ✅ Dependências: Instaladas / Installed
  ✅ ROCm HIP SDK: $(hipcc --version 2>/dev/null | head -1 || echo "OK")
  ✅ Compilação: $BINARY
  ✅ Validação: OK

  Para executar / To run:
    cd $REPO_DIR
    ./BRBtcHuntAMD --help

  Precisou de ajuda? https://github.com/jacazul-ai/BitcoinToolsAMD/issues
  Need help? https://github.com/jacazul-ai/BitcoinToolsAMD/issues
" \
"
  Resumo / Summary:
  ───────────────────────────────────
  ✅ Root / Admin: OK
  ✅ Distribution: $DISTRO_NAME $DISTRO_VER
  ✅ Dependencies: Instaladas / Installed
  ✅ ROCm HIP SDK: $(hipcc --version 2>/dev/null | head -1 || echo "OK")
  ✅ Build: $BINARY
  ✅ Validation: OK

  Para executar / To run:
    cd $REPO_DIR
    ./BRBtcHuntAMD --help

  Precisou de ajuda? https://github.com/jacazul-ai/BitcoinToolsAMD/issues
  Need help? https://github.com/jacazul-ai/BitcoinToolsAMD/issues
"
