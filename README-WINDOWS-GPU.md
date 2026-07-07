# 🪟 Windows GPU Passthrough — BRBtcHuntAMD

## Status
- ✅ GRUB configurado com `amd_iommu=on iommu=pt vfio-pci.ids=1002:73ff,1002:ab28`
- ✅ Initramfs com módulos `vfio`, `vfio_iommu_type1`, `vfio-pci`
- ✅ Script de inicialização: `./run_windows_gpu.sh`
- ✅ Docker image `dockurr/windows` baixada
- ⏳ Pendente: reboot e teste

## Para continuar após reboot

```bash
# 1) Verificar se GPU está no vfio-pci
lspci -k -s 03:00.0
# Deve mostrar: Kernel driver in use: vfio-pci

# 2) Iniciar Windows VM
sudo ./run_windows_gpu.sh

# 3) Acessar via browser
# http://<ip>:8006

# 4) Dentro do Windows 11:
#    - Instalar AMD HIP SDK (https://rocm.docs.amd.com/projects/install-on-windows/)
#    - Git clone https://github.com/jmr2704/BRBtcHuntAMD.git
#    - Compilar com hipcc
#    - Testar com --range 22382FACD0:22382FACD0 --address 1HBtAp...
```

## Para voltar GPU ao host Linux

```bash
sudo sed -i 's/ amd_iommu=on iommu=pt vfio-pci.ids=1002:73ff,1002:ab28//' /etc/default/grub
# Ou复原 o arquivo original manualmente

sudo update-grub
sudo update-initramfs -u
sudo reboot
```

## Hardware
- RX 6600: `0000:03:00.0` (1002:73ff)
- RX 6600 Audio: `0000:03:00.1` (1002:ab28)
- iGPU: `0000:0d:00.0` (1002:1638) — fica no host
