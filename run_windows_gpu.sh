#!/usr/bin/env bash
# ── Rodar Windows VM com GPU Passthrough (dockur/windows) ──
# Uso: sudo ./run_windows_gpu.sh
# Requer: IOMMU + VFIO configurado (já feito via GRUB)

set -euo pipefail

# ── Config ──────────────────────────────────────────────
RX6600_BDF="0000:03:00.0"
RX6600_BDF_AUDIO="0000:03:00.1"
STORAGE="/srv/windows-vm"
RAM="8G"
CPU="4"

# ── Verificar VFIO ──────────────────────────────────────
driver=$(lspci -k -s "$RX6600_BDF" 2>/dev/null | grep "Kernel driver in use" | awk '{print $NF}')
echo "🔍 GPU driver: ${driver:-NENHUM}"

if [ "$driver" != "vfio-pci" ]; then
  echo "⚠️ GPU NAO está no vfio-pci!"
  echo "   O Reboot com os parametros GRUB ainda não foi aplicado."
  echo ""
  echo "   Execute: sudo reboot"
  echo "   Depois rode este script novamente."
  exit 1
fi

# ── IOMMU Group ────────────────────────────────────────
group=$(basename "$(readlink -f /sys/bus/pci/devices/$RX6600_BDF/iommu_group 2>/dev/null)" 2>/dev/null || echo "")
if [ -z "$group" ]; then
  echo "❌ IOMMU group nao encontrado!"
  exit 1
fi
echo "📦 IOMMU group: $group"

# Verificar devices do grupo
echo "   Devices no grupo $group:"
ls /sys/kernel/iommu_groups/$group/devices/ 2>/dev/null | while read dev; do
  echo "     - $dev $(lspci -s $dev 2>/dev/null | cut -d' ' -f2-)" 
done

# ── Storage ─────────────────────────────────────────────
mkdir -p "$STORAGE"

# ── Montar ARGUMENTOS QEMU ─────────────────────────────
QEMU_ARGS="-device vfio-pci,host=$RX6600_BDF,multifunction=on -device vfio-pci,host=$RX6600_BDF_AUDIO"

# ── VFIO devices p/ Docker ─────────────────────────────
VFIO_DEVICES="--device /dev/vfio/vfio"
for dev in /dev/vfio/$group; do
  if [ -e "$dev" ]; then
    VFIO_DEVICES+=" --device $dev"
  fi
done

echo ""
echo "🚀 Iniciando Windows VM com GPU Passthrough..."
echo "   Acessar via browser: http://$(hostname -I | awk '{print $1}'):8006"
echo "   Ou RDP: $(hostname -I | awk '{print $1}') porta 3389"
echo ""

sudo docker run -it --rm \
  --device /dev/kvm \
  $VFIO_DEVICES \
  -v "$STORAGE:/storage" \
  -e "RAM_SIZE=$RAM" \
  -e "CPU_CORES=$CPU" \
  -e "DISK_SIZE=80G" \
  -e "VERSION=11" \
  -e "ARGUMENTS=$QEMU_ARGS" \
  -p 8006:8006 \
  -p 3389:3389 \
  dockurr/windows
