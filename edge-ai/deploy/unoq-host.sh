#!/usr/bin/env bash
#
# unoq-host.sh — tim bo Arduino UNO Q trong mang noi bo. DUNG DE `source`.
#
#     source edge-ai/deploy/unoq-host.sh
#     unoq_find_host            # dat UNOQ_HOST va UNOQ_SSH
#
# VI SAO KHONG GHIM DIA CHI: da mat thoi gian hai lan vi dia chi chu khong phai
# vi code.
#   - router cap DHCP nen bo tu doi 192.168.1.7 -> .11
#   - ten mDNS BreezeLink.local luc phan giai duoc luc khong (avahi tra IPv6)
# Ca hai lan trieu chung deu la "Permission denied" hoac "Connection timed out" —
# trong y het nhu hong key hoac bo chet, chu khong giong sai dia chi. Nen dua viec
# tim bo cho may lam.
#
# Cach nhan ra bo: co /sys/class/bluetooth/hci0 VA kien truc aarch64. Quan trong
# vi trong nha con may Debian khac, va cai nham dich vu len may khac la chuyen co
# that.

UNOQ_USER="${UNOQ_USER:-arduino}"
UNOQ_SSH_OPTS="${UNOQ_SSH_OPTS:--o ConnectTimeout=8 -o BatchMode=yes}"

# Hoi mot may xem co phai bo khong. In ra "<kien-truc> <danh-sach-hci>" neu duoc.
unoq_probe() {
  ssh $UNOQ_SSH_OPTS -o ConnectTimeout=2 "${UNOQ_USER}@$1" \
    'echo "$(uname -m) $(ls /sys/class/bluetooth 2>/dev/null | tr "\n" "," )"' 2>/dev/null
}

# Dat UNOQ_HOST + UNOQ_SSH. Tra 1 neu khong tim thay.
# Tham so 1 (tuy chon): dia chi/ten thu truoc tien.
unoq_find_host() {
  local first="${1:-${UNOQ_HOST:-BreezeLink.local}}"
  local facts base cand f i

  facts="$(unoq_probe "$first")"
  case "$facts" in
    *hci0*) UNOQ_HOST="$first" ;;
    *)
      # Dai mang lay tu chinh may nay, khong ghim 192.168.1.x.
      base="$(ip -4 -o addr 2>/dev/null | awk '/inet 192|inet 10|inet 172/{split($4,a,"."); print a[1]"."a[2]"."a[3]; exit}')"
      [ -n "$base" ] || base="192.168.1"
      for i in $(seq 2 60); do
        cand="${base}.${i}"
        f="$(unoq_probe "$cand")" || continue
        case "$f" in *hci0*) UNOQ_HOST="$cand"; facts="$f"; break ;; esac
      done
      ;;
  esac

  case "$facts" in
    *hci0*) ;;
    *) return 1 ;;
  esac

  UNOQ_SSH="ssh $UNOQ_SSH_OPTS ${UNOQ_USER}@${UNOQ_HOST}"
  UNOQ_FACTS="$facts"
  return 0
}
