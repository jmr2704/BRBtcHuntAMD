<#
.SYNOPSIS
    Automated build environment setup for BRBtcHuntAMD (Windows)
.DESCRIPTION
    Installs VS Build Tools, ROCm HIP SDK, CMake, Git, and compiles BRBtcHuntAMD.
    Must be run as Administrator.
    Idiomas: Portugues (pt-BR) / English (en)
.PARAMETER Language
    Display language: "pt" (default) or "en"
.EXAMPLE
    .\setup-windows.ps1
    .\setup-windows.ps1 -Language en
#>

param(
    [string]$Language = "pt"
)

# -- Strict mode ----------------------------------------------------------
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# -- Paths -----------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Walk up the directory tree to find the repo root (where src/main.cpp exists)
$RepoDir = $ScriptDir
while ($RepoDir -ne (Split-Path $RepoDir -Parent)) {
    if (Test-Path "$RepoDir\src\main.cpp") { break }
    $RepoDir = Split-Path $RepoDir -Parent
}
if (-not (Test-Path "$RepoDir\src\main.cpp")) {
    $RepoDir = Resolve-Path "$ScriptDir\.."  # fallback
}
$LogFile = "$RepoDir\setup-windows.log"

# -- Logging helpers -------------------------------------------------------
function Write-Msg {
    param([string]$Pt, [string]$En)
    $msg = if ($Language -eq "pt") { $Pt } else { $En }
    Write-Host ">> $msg" -ForegroundColor Cyan
}

function Write-OK {
    param([string]$Pt, [string]$En)
    $msg = if ($Language -eq "pt") { $Pt } else { $En }
    Write-Host "[OK] $msg" -ForegroundColor Green
}

function Write-Err {
    param([string]$Pt, [string]$En)
    $msg = if ($Language -eq "pt") { $Pt } else { $En }
    Write-Host "[ERR] $msg" -ForegroundColor Red
}

function Write-Warn {
    param([string]$Pt, [string]$En)
    $msg = if ($Language -eq "pt") { $Pt } else { $En }
    Write-Host "[WARN] $msg" -ForegroundColor Yellow
}

function Write-Banner {
    param([string]$Pt, [string]$En)
    $msg = if ($Language -eq "pt") { $Pt } else { $En }
    Write-Host "=== $msg ===" -ForegroundColor Magenta
}

function Exit-WithError {
    param([string]$Pt, [string]$En)
    Write-Err -Pt $Pt -En $En
    Write-Msg -Pt "Log salvo em: $LogFile" -En "Log saved to: $LogFile"
    exit 1
}

# --------------------------------------------------------------------------
# Etapa 1: Validacao de Administrador
# --------------------------------------------------------------------------
Write-Host ""
Write-Banner -Pt "Instalador automatico - BRBtcHuntAMD (Windows)" `
             -En "Automated Setup - BRBtcHuntAMD (Windows)"
Write-Msg -Pt "Repo: $RepoDir" -En "Repo: $RepoDir"
Write-Host ""

Write-Host ("-" * 74)
Write-Msg -Pt "Etapa 1/6 - Verificando permissao..." `
          -En "Step 1/6 - Checking permissions..."

$IsAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $IsAdmin) {
    Write-Host ""
    Write-Err -Pt "Este script precisa ser executado como ADMINISTRADOR!" `
              -En "This script must be run as ADMINISTRATOR!"
    Write-Host ""
    Write-Msg -Pt "Como executar:" `
              -En "How to run:"
    Write-Msg -Pt "  1. Clique com o botao direito no PowerShell" `
              -En "  1. Right-click on PowerShell"
    Write-Msg -Pt "  2. Selecione 'Executar como Administrador'" `
              -En "  2. Select 'Run as Administrator'"
    Write-Msg -Pt "  3. Navegue ate a pasta: cd $RepoDir" `
              -En "  3. Navigate to: cd $RepoDir"
    Write-Msg -Pt "  4. Execute: .\scripts\setup-windows.ps1" `
              -En "  4. Run: .\scripts\setup-windows.ps1"
    Write-Host ""
    Write-Err -Pt "Motivo: instalacao de SDK, ferramentas de build e configuracao de sistema requerem privilegios de administrador." `
              -En "Reason: SDK installation, build tools, and system configuration require administrator privileges."
    exit 1
}

Write-OK -Pt "Permissao de Administrador confirmada." `
         -En "Administrator permission confirmed."
Write-Host ""

# --------------------------------------------------------------------------
# Etapa 2: Deteccao do ambiente Windows
# --------------------------------------------------------------------------
Write-Host ("-" * 74)
Write-Msg -Pt "Etapa 2/6 - Detectando ambiente Windows..." `
          -En "Step 2/6 - Detecting Windows environment..."

$OsInfo = Get-CimInstance -ClassName Win32_OperatingSystem
$WinVer = [Environment]::OSVersion.Version
$IsWin10OrLater = $WinVer.Major -ge 10

Write-Msg -Pt "Windows detectado: $($OsInfo.Caption) (versao $($WinVer.Major).$($WinVer.Minor))" `
          -En "Windows detected: $($OsInfo.Caption) (version $($WinVer.Major).$($WinVer.Minor))"

if (-not $IsWin10OrLater) {
    Write-Warn -Pt "Windows 10/11 e recomendado. Versoes anteriores podem nao ser compativeis com o HIP SDK." `
               -En "Windows 10/11 is recommended. Older versions may not be compatible with the HIP SDK."
}

# Verificar winget
$WingetAvailable = $false
try {
    $wingetVersion = winget --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        $WingetAvailable = $true
        Write-OK -Pt "winget disponivel" -En "winget available"
    }
} catch {
    Write-Warn -Pt "winget nao encontrado. Tentando chocolatey..." `
               -En "winget not found. Trying chocolatey..."
}

# Verificar chocolatey como fallback
$ChocoAvailable = $false
try {
    $chocoVersion = choco --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        $ChocoAvailable = $true
        Write-OK -Pt "Chocolatey disponivel" -En "Chocolatey available"
    }
} catch {
    Write-Warn -Pt "Chocolatey tambem nao encontrado." `
               -En "Chocolatey also not found."
}

if (-not $WingetAvailable -and -not $ChocoAvailable) {
    Write-Warn -Pt "Nenhum gerenciador de pacotes encontrado. As instalacoes serao feitas via download direto." `
               -En "No package manager found. Installations will be done via direct download."
}
Write-Host ""

# --------------------------------------------------------------------------
# Etapa 3: Instalacao de dependencias (VS Build Tools, CMake, Git, MSYS2)
# --------------------------------------------------------------------------
Write-Host ("-" * 74)
Write-Msg -Pt "Etapa 3/6 - Instalando dependencias do sistema..." `
          -En "Step 3/6 - Installing system dependencies..."

# -- 3a: Visual Studio Build Tools -----------------------------------------
function Install-VSBuildTools {
    Write-Msg -Pt "Verificando Visual Studio Build Tools..." `
              -En "Checking Visual Studio Build Tools..."

    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsInstalled = $false

    if (Test-Path $vsWhere) {
        $vsInfo = & $vsWhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsInfo) {
            Write-OK -Pt "Visual Studio Build Tools encontrado: $vsInfo" `
                     -En "Visual Studio Build Tools found: $vsInfo"
            return $true
        }
    }

    Write-Msg -Pt "VS Build Tools nao encontrado. Iniciando download..." `
              -En "VS Build Tools not found. Starting download..."

    $installerUrl = "https://aka.ms/vs/17/release/vs_BuildTools.exe"
    $installerPath = "$env:TEMP\vs_BuildTools.exe"

    try {
        Write-Msg -Pt "Baixando VS Build Tools 2022..." `
                  -En "Downloading VS Build Tools 2022..."
        Invoke-WebRequest -Uri $installerUrl -OutFile $installerPath -UseBasicParsing
    } catch {
        Write-Err -Pt "Falha ao baixar VS Build Tools.
  Baixe manualmente: https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
  Execute o instalador e selecione:
    - Desenvolvimento para desktop com C++
    - Ferramentas de build do VC++ v143
    - SDK do Windows 10/11" `
                  -En "Failed to download VS Build Tools.
  Download manually: https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
  Run the installer and select:
    - Desktop development with C++
    - VC++ v143 build tools
    - Windows 10/11 SDK"
        return $false
    }

    Write-Msg -Pt "Instalando VS Build Tools (isso pode levar varios minutos)..." `
              -En "Installing VS Build Tools (this may take several minutes)..."

    $proc = Start-Process -FilePath $installerPath -ArgumentList "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --includeRecommended" -NoNewWindow -PassThru -Wait
    if ($proc.ExitCode -eq 0 -or $proc.ExitCode -eq 3010) {
        Write-OK -Pt "VS Build Tools instalado com sucesso!" `
                 -En "VS Build Tools installed successfully!"
        if ($proc.ExitCode -eq 3010) {
            Write-Warn -Pt "Reinicializacao necessaria para concluir a instalacao do VS." `
                       -En "Reboot required to complete VS installation."
        }
        return $true
    } else {
        Write-Err -Pt "Falha na instalacao do VS Build Tools (codigo: $($proc.ExitCode)).
  Tente manualmente:
    1. Baixe de: https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
    2. Execute: vs_BuildTools.exe
    3. Selecione 'Ferramentas de build do C++'
    4. Complete a instalacao" `
                  -En "VS Build Tools installation failed (code: $($proc.ExitCode)).
  Try manually:
    1. Download from: https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
    2. Run: vs_BuildTools.exe
    3. Select 'C++ build tools'
    4. Complete the installation"
        return $false
    }
}

# -- 3b: CMake -------------------------------------------------------------
function Install-CMake {
    if (Get-Command cmake -ErrorAction SilentlyContinue) {
        Write-OK -Pt "CMake ja instalado: $(cmake --version | Select-Object -First 1)" `
                 -En "CMake already installed: $(cmake --version | Select-Object -First 1)"
        return $true
    }

    Write-Msg -Pt "Instalando CMake..." -En "Installing CMake..."

    if ($WingetAvailable) {
        $result = winget install Kitware.CMake --silent --accept-package-agreements 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-OK -Pt "CMake instalado via winget!" -En "CMake installed via winget!"
            return $true
        }
        Write-Warn -Pt "winget falhou, tentando metodo alternativo..." `
                   -En "winget failed, trying alternative method..."
    }

    if ($ChocoAvailable) {
        $result = choco install cmake -y 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-OK -Pt "CMake instalado via Chocolatey!" -En "CMake installed via Chocolatey!"
            return $true
        }
        Write-Warn -Pt "Chocolatey falhou, tentando download direto..." `
                   -En "Chocolatey failed, trying direct download..."
    }

    # Direct download fallback
    try {
        $cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v3.31.0/cmake-3.31.0-windows-x86_64.msi"
        $cmakePath = "$env:TEMP\cmake-install.msi"
        Invoke-WebRequest -Uri $cmakeUrl -OutFile $cmakePath -UseBasicParsing
        Start-Process msiexec.exe -ArgumentList "/i `"$cmakePath`" /quiet /norestart" -NoNewWindow -Wait
        Write-OK -Pt "CMake instalado via download direto!" `
                 -En "CMake installed via direct download!"
        return $true
    } catch {
        Write-Err -Pt "Falha ao instalar CMake.
  Baixe manualmente: https://cmake.org/download/
  Execute o instalador e marque 'Add CMake to system PATH'." `
                  -En "Failed to install CMake.
  Download manually: https://cmake.org/download/
  Run the installer and check 'Add CMake to system PATH'."
        return $false
    }
}

# -- 3c: Git ---------------------------------------------------------------
function Install-Git {
    if (Get-Command git -ErrorAction SilentlyContinue) {
        Write-OK -Pt "Git ja instalado: $(git --version)" `
                 -En "Git already installed: $(git --version)"
        return $true
    }

    Write-Msg -Pt "Instalando Git..." -En "Installing Git..."

    if ($WingetAvailable) {
        winget install Git.Git --silent --accept-package-agreements 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-OK -Pt "Git instalado via winget!" -En "Git installed via winget!"
            # Refresh PATH
            $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
            return $true
        }
    }

    if ($ChocoAvailable) {
        choco install git -y 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-OK -Pt "Git instalado via Chocolatey!" -En "Git installed via Chocolatey!"
            $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
            return $true
        }
    }

    # Direct download fallback
    try {
        $gitUrl = "https://github.com/git-for-windows/git/releases/download/v2.47.1.windows.1/Git-2.47.1-64-bit.exe"
        $gitPath = "$env:TEMP\git-install.exe"
        Write-Msg -Pt "Baixando Git via download direto..." `
                  -En "Downloading Git via direct download..."
        Invoke-WebRequest -Uri $gitUrl -OutFile $gitPath -UseBasicParsing
        Write-Msg -Pt "Instalando Git (isto pode levar alguns minutos)..." `
                  -En "Installing Git (this may take a few minutes)..."
        Start-Process -FilePath $gitPath -ArgumentList '/VERYSILENT /NORESTART /NOCANCEL /SP- /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS /COMPONENTS="icons,ext,gitlfs,assoc,autoupdate"' -NoNewWindow -Wait
        $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
        if (Get-Command git -ErrorAction SilentlyContinue) {
            Write-OK -Pt "Git instalado via download direto!" `
                     -En "Git installed via direct download!"
            return $true
        }
    } catch {
        Write-Warn -Pt "Download direto do Git falhou: $($_.Exception.Message)" `
                   -En "Git direct download failed: $($_.Exception.Message)"
    }

    Write-Err -Pt "Git nao pode ser instalado automaticamente.
  Baixe manualmente: https://git-scm.com/download/win
  Durante a instalacao, selecione 'Git from the command line and also from 3rd-party software'." `
              -En "Git could not be installed automatically.
  Download manually: https://git-scm.com/download/win
  During installation, select 'Git from the command line and also from 3rd-party software'."
    return $false
}

# -- 3d: MSYS2 (for make.exe) ----------------------------------------------
function Install-Msys2 {
    if (Get-Command make -ErrorAction SilentlyContinue) {
        Write-OK -Pt "make ja disponivel: $(make --version | Select-Object -First 1)" `
                 -En "make already available: $(make --version | Select-Object -First 1)"
        return $true
    }

    # Check if make is available via Git Bash
    $gitMakePath = "${env:ProgramFiles}\Git\usr\bin\make.exe"
    if (Test-Path $gitMakePath) {
        Write-OK -Pt "make disponivel via Git Bash!" -En "make available via Git Bash!"
        $env:Path += ";${env:ProgramFiles}\Git\usr\bin"
        return $true
    }

    Write-Warn -Pt "make nao encontrado. Tentando instalar MSYS2..." `
               -En "make not found. Trying to install MSYS2..."

    if ($WingetAvailable) {
        winget install MSYS2.MSYS2 --silent --accept-package-agreements 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            # Add MSYS2 usr/bin to PATH for make
            $msys2Path = "$env:ProgramFiles\MSYS2\usr\bin"
            if (Test-Path "$msys2Path\make.exe") {
                $env:Path += ";$msys2Path"
                Write-OK -Pt "MSYS2 instalado e make disponivel!" `
                         -En "MSYS2 installed and make available!"
                return $true
            }
        }
    }

    Write-Warn -Pt "MSYS2 nao pode ser instalado. A compilacao sera feita via comando direto do hipcc (sem make)." `
               -En "MSYS2 could not be installed. Build will use direct hipcc command (without make)."
    return $false
}

# Execute installations
$vsOk = Install-VSBuildTools
$cmakeOk = Install-CMake
$gitOk = Install-Git
$msysOk = Install-Msys2

if (-not $vsOk) {
    Exit-WithError -Pt "VS Build Tools e obrigatorio. Instale manualmente e tente novamente." `
                   -En "VS Build Tools is required. Install manually and try again."
}

Write-OK -Pt "Dependencias do sistema configuradas." `
         -En "System dependencies configured."
Write-Host ""

# --------------------------------------------------------------------------
# Etapa 4: Instalacao do ROCm HIP SDK for Windows
# --------------------------------------------------------------------------
Write-Host ("-" * 74)
Write-Msg -Pt "Etapa 4/6 - Instalando ROCm HIP SDK for Windows..." `
          -En "Step 4/6 - Installing ROCm HIP SDK for Windows..."

function Test-HipInstalled {
    if (Get-Command hipcc -ErrorAction SilentlyContinue) {
        Write-OK -Pt "hipcc encontrado: $(hipcc --version 2>&1 | Select-Object -First 1)" `
                 -En "hipcc found: $(hipcc --version 2>&1 | Select-Object -First 1)"
        return $true
    }
    # Verificar caminhos comuns
    $commonPaths = @(
        "$env:ProgramFiles\AMD\ROCm\*\bin\hipcc.exe",
        "$env:ProgramFiles\AMD\ROCm\bin\hipcc.exe",
        "C:\Program Files\AMD\ROCm\*\bin\hipcc.exe"
    )
    foreach ($pattern in $commonPaths) {
        $matches = Get-ChildItem $pattern -ErrorAction SilentlyContinue
        if ($matches) {
            $hipccPath = $matches[0].FullName
            Write-OK -Pt "hipcc encontrado em: $hipccPath" `
                     -En "hipcc found at: $hipccPath"
            $env:Path += ";$(Split-Path $hipccPath -Parent)"
            return $true
        }
    }
    return $false
}

if (Test-HipInstalled) {
    Write-OK -Pt "ROCm HIP SDK ja esta instalado. Pulando instalacao." `
             -En "ROCm HIP SDK is already installed. Skipping installation."
} else {
    Write-Msg -Pt "ROCm HIP SDK nao encontrado. Iniciando instalacao..." `
              -En "ROCm HIP SDK not found. Starting installation..."

    # Try winget first
    $hipInstalled = $false
    if ($WingetAvailable) {
        Write-Msg -Pt "Tentando via winget: AMD.ROCm..." `
                  -En "Trying via winget: AMD.ROCm..."
        $result = winget install AMD.ROCm --silent --accept-package-agreements 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-OK -Pt "ROCm instalado via winget!" -En "ROCm installed via winget!"
            $hipInstalled = $true
        } else {
            Write-Warn -Pt "winget nao encontrou AMD.ROCm. Tentando download direto..." `
                       -En "winget did not find AMD.ROCm. Trying direct download..."
        }
    }

    # Direct download fallback
    if (-not $hipInstalled) {
        Write-Msg -Pt "Baixando ROCm HIP SDK do site oficial da AMD..." `
                  -En "Downloading ROCm HIP SDK from AMD's official site..."

        # ROCm for Windows download URL - AMD changes this frequently.
        # Primary: try winget or chocolatey first; direct download is last resort.
        $rocmUrl = "https://repo.radeon.com/rocm/install/latest/windows/AMD-ROCm-SDK.exe"
        $rocmInstallerPath = "$env:TEMP\AMD-ROCm-SDK.exe"

        try {
            Write-Msg -Pt "Download do instalador ROCm (aproximadamente 2-3 GB)..." `
                      -En "Downloading ROCm installer (approximately 2-3 GB)..."
            # Use WebClient for progress display
            $wc = New-Object System.Net.WebClient
            $wc.DownloadFile($rocmUrl, $rocmInstallerPath)
        } catch {
            Write-Err -Pt "Falha ao baixar o instalador ROCm.
  Baixe manualmente de: https://www.amd.com/en/developer/rocm.html
  1. Acesse https://rocm.docs.amd.com/en/latest/deploy/windows/install.html
  2. Baixe o instalador do ROCm for Windows
  3. Execute o instalador com as opcoes padrao
  4. Apos a instalacao, execute este script novamente" `
                      -En "Failed to download ROCm installer.
  Download manually from: https://www.amd.com/en/developer/rocm.html
  1. Visit https://rocm.docs.amd.com/en/latest/deploy/windows/install.html
  2. Download the ROCm for Windows installer
  3. Run the installer with default options
  4. After installation, run this script again"
            
            # Alternative: try chocolatey
            if ($ChocoAvailable) {
                Write-Msg -Pt "Tentando via Chocolatey..." -En "Trying via Chocolatey..."
                choco install rocm -y 2>&1 | Out-Null
                if ($LASTEXITCODE -eq 0) {
                    Write-OK -Pt "ROCm instalado via Chocolatey!" -En "ROCm installed via Chocolatey!"
                    $hipInstalled = $true
                }
            }
            
            if (-not $hipInstalled) {
                Exit-WithError -Pt "Nao foi possivel instalar o ROCm HIP SDK automaticamente. Instale manualmente." `
                               -En "Could not install ROCm HIP SDK automatically. Install manually."
            }
        }

        if (-not $hipInstalled -and (Test-Path $rocmInstallerPath)) {
            Write-Msg -Pt "Executando instalador ROCm (isto pode levar varios minutos)..." `
                      -En "Running ROCm installer (this may take several minutes)..."
            $proc = Start-Process -FilePath $rocmInstallerPath -ArgumentList "--quiet" -NoNewWindow -PassThru -Wait
            
            if ($proc.ExitCode -eq 0 -or $proc.ExitCode -eq 3010) {
                Write-OK -Pt "ROCm HIP SDK instalado com sucesso!" `
                         -En "ROCm HIP SDK installed successfully!"
                $hipInstalled = $true
                if ($proc.ExitCode -eq 3010) {
                    Write-Warn -Pt "Reinicializacao necessaria para concluir a instalacao do ROCm." `
                               -En "Reboot required to complete ROCm installation."
                }
            } else {
                Exit-WithError -Pt "Falha na instalacao do ROCm (codigo: $($proc.ExitCode)).
  Tente manualmente:
    1. Baixe o instalador de: https://www.amd.com/en/developer/rocm.html
    2. Execute como Administrador
    3. Selecione a instalacao completa
    4. Reinicie o computador apos a instalacao" `
                               -En "ROCm installation failed (code: $($proc.ExitCode)).
  Try manually:
    1. Download from: https://www.amd.com/en/developer/rocm.html
    2. Run as Administrator
    3. Select full installation
    4. Reboot after installation"
            }
        }
    }

    # Final verification
    if (-not (Test-HipInstalled)) {
        # Refresh PATH from registry
        $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
        if (-not (Test-HipInstalled)) {
            Exit-WithError -Pt "ROCm foi instalado mas hipcc nao esta no PATH.
  Tente:
    1. Abra um novo PowerShell como Administrador
    2. Execute: \$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    3. Execute este script novamente
  Ou adicione manualmente ao PATH:
    C:\Program Files\AMD\ROCm\bin" `
                           -En "ROCm was installed but hipcc is not in PATH.
  Try:
    1. Open a new PowerShell as Administrator
    2. Run: \$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    3. Run this script again
  Or manually add to PATH:
    C:\Program Files\AMD\ROCm\bin"
        }
    }
}
Write-Host ""

# --------------------------------------------------------------------------
# Etapa 5: Compilacao do BRBtcHuntAMD
# --------------------------------------------------------------------------
Write-Host ("-" * 74)
Write-Msg -Pt "Etapa 5/6 - Compilando BRBtcHuntAMD..." `
          -En "Step 5/6 - Building BRBtcHuntAMD..."

cd $RepoDir

# Verificar se o diretorio src existe
if (-not (Test-Path "$RepoDir\src\main.cpp")) {
    Exit-WithError -Pt "Diretorio 'src' nao encontrado em $RepoDir.
  Certifique-se de estar no diretorio correto do repositorio." `
                   -En "'src' directory not found in $RepoDir.
  Make sure you're in the correct repository directory."
}

# Tentar compilar
$BuildSuccess = $false

# Strategy 1: Use make if available
if (Get-Command make -ErrorAction SilentlyContinue) {
    Write-Msg -Pt "Usando make para compilar..." `
              -En "Using make to build..."
    
    # Clean first
    & make clean 2>&1 | Out-Null
    
    $buildOutput = & make -j4 2>&1
    $buildExitCode = $LASTEXITCODE
    
    if ($buildExitCode -eq 0) {
        Write-OK -Pt "Compilacao com make concluida!" -En "Build with make completed!"
        $BuildSuccess = $true
    } else {
        Write-Warn -Pt "make falhou. Tentando metodo alternativo..." `
                   -En "make failed. Trying alternative method..."
        Write-Msg -Pt "Erro do make:" -En "make error:"
        Write-Host $buildOutput
    }
}

# Strategy 2: Direct hipcc compilation (fallback)
if (-not $BuildSuccess) {
    Write-Msg -Pt "Compilando diretamente com hipcc..." `
              -En "Building directly with hipcc..."

    # Find available GPU architectures
    $gpuArch = "gfx1032"  # Default: RX 6600
    
    if (Get-Command rocminfo -ErrorAction SilentlyContinue) {
        $gpuInfo = & rocminfo 2>&1 | Select-String "gfx[0-9a-f]+"
        if ($gpuInfo) {
            $detected = $gpuInfo.Matches.Value | Select-Object -Unique
            if ($detected) {
                $gpuArch = $detected -join ' '
                Write-Msg -Pt "Arquitetura(s) de GPU detectada(s): $gpuArch" `
                          -En "Detected GPU architecture(s): $gpuArch"
            }
        }
    } else {
        # Try WMI detection as fallback
        try {
            $gpu = Get-CimInstance -ClassName Win32_VideoController | Where-Object { $_.Name -match 'AMD|Radeon|Radeon' } | Select-Object -First 1
            if ($gpu) {
                Write-Msg -Pt "GPU detectada via WMI: $($gpu.Name)" `
                          -En "GPU detected via WMI: $($gpu.Name)"
                # Map common AMD GPUs to gfx arch
                if ($gpu.Name -match 'RX 5[67]00|Vega|VII') { $gpuArch = 'gfx906' }
                elseif ($gpu.Name -match 'RX 5500|RX 5600|RX 5700|Navi 1[04]') { $gpuArch = 'gfx1010' }
                elseif ($gpu.Name -match 'RX 6[45]00|RX 6600|RX 6700|RX 6800|RX 6900|Navi 2') { $gpuArch = 'gfx1030' }
                elseif ($gpu.Name -match 'RX 7600|RX 7700|RX 7800|RX 7900|Navi 3') { $gpuArch = 'gfx1100' }
            }
        } catch {
            # WMI failed, keep defaults
        }
    }

    if (-not $gpuArch) { $gpuArch = 'gfx1032' }
    Write-Msg -Pt "Usando arquitetura(s): $gpuArch" `
              -En "Using architecture(s): $gpuArch"

    # Convert space-separated arches to --offload-arch flags
    $gencodeFlags = ($gpuArch -split '\s+' | ForEach-Object { "--offload-arch=$_" }) -join ' '
    Write-Msg -Pt "Compilando com arquitetura(s): $gpuArch" `
              -En "Building with architecture(s): $gpuArch"
    
    # Create obj directory if needed
    if (-not (Test-Path "$RepoDir\obj")) {
        New-Item -ItemType Directory -Path "$RepoDir\obj" -Force | Out-Null
    }

    # Ensure ROCm bin is in PATH for hipcc
    $rocmDirs = Get-ChildItem "C:\Program Files\AMD\ROCm\*\bin" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($rocmDirs) {
        $env:Path = "$($rocmDirs.FullName);$env:Path"
    }

    
    # Temporarily disable stop-on-error for hipcc (it may emit harmless warnings to stderr)
    $prevErrorPref = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    Write-Msg -Pt "Compilando main.cpp..." -En "Compiling main.cpp..."
    $output1 = & hipcc -O3 -ffast-math -std=c++17 -I"$RepoDir\include" -DBTC_GPU_BACKEND_HIP=1 $gencodeFlags -c "$RepoDir\src\main.cpp" -o "$RepoDir\obj\main.o" -Wno-unused-result -Wno-ignored-attributes 2>&1
    $exit1 = $LASTEXITCODE
    if ($exit1 -ne 0) {
        Write-Err -Pt "Falha ao compilar maincpp:" -En "Failed to compile main.cpp:"
        Write-Host $output1
        $ErrorActionPreference = $prevErrorPref
        Exit-WithError -Pt "Erro na compilacao do main.cpp. Verifique a instalacao do HIP SDK." `
                       -En "main.cpp compilation failed. Check HIP SDK installation."
    }

    Write-Msg -Pt "Compilando GPUWorker.cpp..." -En "Compiling GPUWorker.cpp..."
    $output2 = & hipcc -O3 -ffast-math -std=c++17 -I"$RepoDir\include" -DBTC_GPU_BACKEND_HIP=1 $gencodeFlags -c "$RepoDir\src\GPUWorker.cpp" -o "$RepoDir\obj\GPUWorker.o" -Wno-unused-result -Wno-ignored-attributes 2>&1
    $exit2 = $LASTEXITCODE
    if ($exit2 -ne 0) {
        Write-Err -Pt "Falha ao compilar GPUWorker.cpp:" -En "Failed to compile GPUWorker.cpp:"
        Write-Host $output2
        $ErrorActionPreference = $prevErrorPref
        Exit-WithError -Pt "Erro na compilacao do GPUWorker.cpp. Verifique a instalacao do HIP SDK." `
                       -En "GPUWorker.cpp compilation failed. Check HIP SDK installation."
    }

    Write-Msg -Pt "Linkando..." -En "Linking..."
    $output3 = & hipcc $gencodeFlags "$RepoDir\obj\main.o" "$RepoDir\obj\GPUWorker.o" -o "$RepoDir\BRBtcHuntAMD.exe" 2>&1
    $exit3 = $LASTEXITCODE
    if ($exit3 -eq 0) {
        Write-OK -Pt "Compilacao concluida!" -En "Build completed!"
        $BuildSuccess = $true
    } else {
        Write-Err -Pt "Falha na linkagem:" -En "Linking failed:"
        Write-Host $output3
        $ErrorActionPreference = $prevErrorPref
        Exit-WithError -Pt "Erro na linkagem. Verifique se as bibliotecas ROCm estao instaladas corretamente." `
                       -En "Linking failed. Verify that ROCm libraries are correctly installed."
    }

    $ErrorActionPreference = $prevErrorPref
}

Write-Host ""

# --------------------------------------------------------------------------
# Etapa 6: Validacao do binario
# --------------------------------------------------------------------------
Write-Host ("-" * 74)
Write-Msg -Pt "Etapa 6/6 - Validando binario..." `
          -En "Step 6/6 - Validating binary..."

$Binary = "$RepoDir\BRBtcHuntAMD.exe"

if (-not (Test-Path $Binary)) {
    Exit-WithError -Pt "Binario nao encontrado: $Binary
  Algo deu errado na compilacao. Verifique os logs acima." `
                   -En "Binary not found: $Binary
  Something went wrong during compilation. Check the logs above."
}

# Test help flag
try {
    $helpOutput = & $Binary --help 2>&1
    if ($LASTEXITCODE -eq 0 -and $helpOutput -match "BRBtcHuntAMD") {
        Write-OK -Pt "Binario compilado e funcionando!" `
                 -En "Binary compiled and working!"
        Write-Msg -Pt "Comando de exemplo:
  $Binary --range 2000000000:3FFFFFFFFF --address 1PWo6GdxJ4CmB6LuDCm9oXxwuunEHNEMk" `
                  -En "Example command:
  $Binary --range 2000000000:3FFFFFFFFF --address 1PWo6GdxJ4CmB6LuDCm9oXxwuunEHNEMk"
    } else {
        Write-Warn -Pt "Binario existe mas --help nao retornou o esperado:" `
                   -En "Binary exists but --help did not return expected output:"
        Write-Host $helpOutput
        Exit-WithError -Pt "Binario parece corrompido ou incompativel. Tente compilar manualmente." `
                       -En "Binary appears corrupted or incompatible. Try building manually."
    }
} catch {
    Exit-WithError -Pt "Falha ao executar o binario.
  Erro: $($_.Exception.Message)
  Verifique se as DLLs do ROCm estao no PATH." `
                   -En "Failed to execute binary.
  Error: $($_.Exception.Message)
  Verify that ROCm DLLs are in your PATH."
}

# --------------------------------------------------------------------------
# Finalizacao
# --------------------------------------------------------------------------
Write-Host ("-" * 74)
Write-Host ""
Write-OK -Pt "Setup concluido com sucesso! *" `
         -En "Setup completed successfully! *"

$Summary = @"
  Resumo / Summary:
  -----------------------------------
  [OK] Admin: OK
  [OK] Windows: $($OsInfo.Caption)
  [OK] VS Build Tools: OK
  [OK] CMake: OK
  [OK] Git: OK
  [OK] ROCm HIP SDK: OK
  [OK] Compilacao: $Binary
  [OK] Validacao: OK

  Para executar / To run:
    cd $RepoDir
    .\BRBtcHuntAMD.exe --help

  Precisou de ajuda? https://github.com/jacazul-ai/BitcoinToolsAMD/issues
  Need help? https://github.com/jacazul-ai/BitcoinToolsAMD/issues
"@
Write-Msg -Pt $Summary -En $Summary
