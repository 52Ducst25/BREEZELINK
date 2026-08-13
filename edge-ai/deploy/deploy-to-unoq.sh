#!/usr/bin/env bash
#
# deploy-to-unoq.sh — cai dich vu edge-AI len nua Linux cua Arduino UNO Q.
#
# VI SAO KHONG DUNG ARDUINO APP LAB: App Lab chay phan Python TRONG CONTAINER
# (log khoi dong ghi ro "Container breezelink-edge-ai-main-1 Started"), va
# container do khong co socket D-Bus cua he thong:
#
#     /run/dbus/system_bus_socket        khong co
#     bluetoothctl                       khong co
#     /sys/class/bluetooth               hci0        <- adapter CO that
#
# bleak noi chuyen voi BlueZ qua D-Bus, nen no chet ngay khi khoi tao voi mot loi
# khong he nhac toi Bluetooth: "[Errno 2] No such file or directory". Danh sach
# Brick cua App Lab cung khong co Bluetooth. Nen duong duy nhat giu duoc BLE la
# chay thang tren he dieu hanh cua bo.
#
#   scripts:  bash edge-ai/deploy/deploy-to-unoq.sh
#
# Auth: dung ssh key. Chua co thi chay mot lan:
#     ssh-copy-id -i ~/.ssh/breezelink_vps_ed25519.pub arduino@192.168.1.7

set -euo pipefail

# TEN mDNS, KHONG PHAI IP. Router cap DHCP nen bo da tu doi 192.168.1.7 -> .11
# mot lan; ghim IP thi moi lenh sau do bao "Permission denied", trong y het nhu
# hong key chu khong phai sai dia chi.
AC_UNOQ_HOST="${AC_UNOQ_HOST:-BreezeLink.local}"
AC_UNOQ_USER="${AC_UNOQ_USER:-arduino}"
AC_ORG_ID="${AC_ORG_ID:-}"
# Trong /home chu khong /opt: sudo tren bo nay DOI MAT KHAU, nen moi buoc can sudo
# la mot buoc khong trien khai tu xa duoc. User `arduino` tu ghi duoc vao home,
# va sudo chi con can dung MOT LAN de dat unit systemd.
REMOTE_DIR="/home/arduino/breezelink"
SSH_OPTS="-o ConnectTimeout=15"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

SSH="ssh $SSH_OPTS ${AC_UNOQ_USER}@${AC_UNOQ_HOST}"

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
ok()  { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

[ -n "$AC_ORG_ID" ] || die "Dat AC_ORG_ID truoc:
     AC_ORG_ID=<org-id> bash edge-ai/deploy/deploy-to-unoq.sh
   Lay o trang 'Nap firmware' cua bat ky node nao tren web."

# ---- 1. dung may khong phai la mot cai gia ----------------------------------
# 192.168.1.7 duoc tim ra bang cach quet cong 22 trong LAN, khong phai duoc khai
# bao o dau ca. Cai nham dich vu len may khac trong nha la chuyen co that, va
# `hci0` + kien truc aarch64 la cap dau van tay re nhat de phan biet.
# Viec tim bo nam o unoq-host.sh — dung chung voi scripts/push-unoq-app.sh.
# Hai ban sao cua logic nay chac chan se lech nhau, ma trieu chung cua lech la
# "script nay tim thay bo, script kia thi khong".
# shellcheck source=unoq-host.sh
UNOQ_USER="$AC_UNOQ_USER"
source "${SCRIPT_DIR}/unoq-host.sh"

say "Tim bo UNO Q"
unoq_find_host "$AC_UNOQ_HOST" || die "Khong tim thay bo UNO Q.
     - Bo da bat va noi WiFi chua?
     - Da cai key chua: ssh-copy-id -i ~/.ssh/breezelink_vps_ed25519.pub ${AC_UNOQ_USER}@<ip>
     - Biet dia chi thi chi dinh: AC_UNOQ_HOST=<ip> bash \$0"
AC_UNOQ_HOST="$UNOQ_HOST"
SSH="$UNOQ_SSH"
FACTS="$UNOQ_FACTS"
case "$FACTS" in
  *hci0*) ok "$FACTS" ;;
  *) die "May nay khong co hci0 (thay: '$FACTS') — gan nhu chac chan khong phai bo UNO Q." ;;
esac

# ---- 2. goi LAT CAT, khong phai ca src/ -------------------------------------
# KHONG CHEP NGUYEN src/ — da thu va no hong:
#
#   File ".../src/app/models/__init__.py", line 7
#     from app.models.app_release import AppRelease
#   ModuleNotFoundError: No module named 'sqlalchemy'
#
# comfort_bridge chi can `app.models.enums.AcMode`, nhung Python chay
# __init__.py cua goi TRUOC khi nap module con, ma ban that cua no import ca 12
# model ORM. Cai SQLAlchemy len bo de lay dung mot enum la sai huong; dung la
# mang theo lat cat va thay __init__.py bang ban rong.
#
# Lat cat do build-edge-payload.py dinh nghia. Goi lai chinh no thay vi chep danh
# sach file sang day: hai danh sach se lech nhau, va trieu chung cua lech la
# ImportError tren bo — cho kho xem log nhat.
say "Dung lat cat backend"
PY="$(command -v python || command -v python3 || command -v py)" \
  || die "Khong tim thay python tren may nay."
PAYLOAD="${SCRIPT_DIR}/payload"
"$PY" "${SCRIPT_DIR}/build-edge-payload.py" --out "$PAYLOAD" >/dev/null \
  || die "Dung lat cat that bai."
[ -d "$PAYLOAD/edge_ai" ] || die "Khong thay $PAYLOAD/edge_ai"
ok "$(find "$PAYLOAD" -type f | wc -l | tr -d ' ') file"

say "Dong goi"
TARBALL="$(mktemp -t breezelink-XXXXXX).tgz"
trap 'rm -f "$TARBALL"' EXIT
tar czf "$TARBALL" -C "$SCRIPT_DIR" \
  --exclude='__pycache__' --exclude='*.pyc' payload
ok "$(du -h "$TARBALL" | cut -f1)"

# ---- 3. chuyen + cai -------------------------------------------------------
say "Chuyen len bo va cai"
scp $SSH_OPTS "$TARBALL" "${AC_UNOQ_USER}@${AC_UNOQ_HOST}:/tmp/breezelink.tgz"

$SSH "set -e
  mkdir -p ${REMOTE_DIR}
  # Xoa han ban cu roi bung lai: mot file da bo khoi dich vu ma con nam tren bo
  # van import duoc, va 'da sua roi ma loi cu van con' la kieu hong ton nhieu
  # thoi gian nhat de tim.
  rm -rf ${REMOTE_DIR}/payload ${REMOTE_DIR}/src ${REMOTE_DIR}/edge-ai
  tar xzf /tmp/breezelink.tgz -C ${REMOTE_DIR}
  rm -f /tmp/breezelink.tgz

  cd ${REMOTE_DIR}
  [ -d .venv ] || python3 -m venv .venv
  # KHONG dat --system-site-packages: bo co the da co bleak ban khac, va mot phien
  # ban khac ngam ben duoi la kieu loi chi lo ra khi da lap xong.
  .venv/bin/pip install --quiet --upgrade pip
  # Cai thang hai goi thay vi 'pip install -e edge-ai/': payload la mot cay module
  # tran, khong mang theo pyproject.toml. Danh sach nay phai khop dependencies
  # trong edge-ai/pyproject.toml — lech la dich vu chet o lan import dau tien.
  .venv/bin/pip install --quiet 'pyserial>=3.5' 'pydantic>=2'

  # KHONG dung dau backtick trong khoi nay: no nam trong mot chuoi nhay kep gui
  # qua ssh, nen shell tren bo se THUC THI phan trong backtick — ke ca khi no chi
  # la chu thich. Da dinh mot lan: hai dong 'app/: No such file or directory'.
  #
  # KHONG can EDGE_BACKEND_SRC: thu muc app/ nam ngay canh edge_ai/ trong payload,
  # nen comfort_bridge tim thay qua trinh nap module (no hoi sys.path truoc khi
  # hoi he thong tep).
  printf 'EDGE_ORG_ID=%s\n' '${AC_ORG_ID}' > ${REMOTE_DIR}/.env
  chmod 600 ${REMOTE_DIR}/.env
"
ok "da cai vao ${REMOTE_DIR}"

say "Kiem import truoc khi bat dich vu"
$SSH "cd ${REMOTE_DIR} && PYTHONPATH=${REMOTE_DIR}/payload .venv/bin/python -c \
  'import edge_ai.main, edge_ai.comfort_bridge as cb, sys;
print(\"  import OK · compute() tu\", cb.compute.__module__);
print(\"  sqlalchemy:\", [m for m in sys.modules if m.startswith(\"sqlalchemy\")] or \"khong bi keo theo\")'" \
  || die "Import that bai — dung lai truoc khi bat dich vu. Xem loi o tren."

# ---- 4. systemd ------------------------------------------------------------
# BUOC DUY NHAT CAN SUDO. Neu sudo khong hoi mat khau thi lam luon; con hoi thi
# IN RA de nguoi dung dan vao terminal cua bo. Khong co duong nao khac: gui mat
# khau sudo qua mot phien ssh khong tuong tac vua khong lam duoc vua khong nen.
say "Cai unit systemd"
scp $SSH_OPTS "${SCRIPT_DIR}/breezelink-edge-ai.service" \
  "${AC_UNOQ_USER}@${AC_UNOQ_HOST}:${REMOTE_DIR}/breezelink-edge-ai.service"
UNIT_CMDS="sudo cp ${REMOTE_DIR}/breezelink-edge-ai.service /etc/systemd/system/ && sudo systemctl daemon-reload && sudo systemctl enable --now breezelink-edge-ai"

if $SSH "sudo -n true" 2>/dev/null; then
  $SSH "set -e; $UNIT_CMDS; sleep 4"
  ok "da bat dich vu"
  say "Nhat ky 20 dong dau"
  $SSH "journalctl -u breezelink-edge-ai -n 20 --no-pager" || true
else
  echo
  say "sudo tren bo doi mat khau — CHAY KHOI NAY TRONG TERMINAL CUA BO:"
  echo
  echo "  $UNIT_CMDS"
  echo
  echo "  journalctl -u breezelink-edge-ai -n 30 --no-pager"
fi

echo
say "Theo doi tiep:"
echo "     ssh ${AC_UNOQ_USER}@${AC_UNOQ_HOST} 'journalctl -u breezelink-edge-ai -f'"
echo "   Phai thay: 'Da noi gateway ... (MTU 247)' roi moi 30s mot dong 't_in=...'"
echo "   Va tren serial gateway (COM14): 'UNO Q da noi'."
