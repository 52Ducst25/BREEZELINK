#!/usr/bin/env bash
#
# push-unoq-app.sh — day mot App len Arduino UNO Q de App Lab nhin thay.
#
#     bash scripts/push-unoq-app.sh <duong-dan-thu-muc-app>
#
# VI SAO CO SCRIPT NAY: App Lab giu app TREN BO va liet ke chung tu
# ~/ArduinoApps/. Da kiem chung: tao mot thu muc o do bang ssh thi App Lab thay
# ngay, khong can bam "Import App". Nho vay VS Code lam noi sua, con App Lab lam
# noi chay va xem log — khong ai phai sua code trong App Lab.
#
# DUNG SUA CODE TRONG APP LAB: no dong bo app tu bo xuong mot thu muc TAM de sua,
# va thu muc tam do bi ghi de lai. Da do: mot file vua ghi vao day quay ve ban goc
# sau vai giay.
#
# Thu muc app phai co dung khuon App Lab:
#     <app>/app.yaml
#     <app>/python/main.py       (+ requirements.txt neu can thu vien)
#     <app>/sketch/sketch.ino    (+ sketch.yaml)
# Thieu sketch/ thi App Lab van liet ke nhung khong Run duoc.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=../edge-ai/deploy/unoq-host.sh
source "${REPO_ROOT}/edge-ai/deploy/unoq-host.sh"

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
ok()  { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

APP_DIR="${1:-}"
[ -n "$APP_DIR" ] || die "Thieu duong dan app.
     bash scripts/push-unoq-app.sh <duong-dan-thu-muc-app>"
[ -d "$APP_DIR" ] || die "Khong thay thu muc: $APP_DIR"
[ -f "$APP_DIR/app.yaml" ] || die "Thieu $APP_DIR/app.yaml — day khong phai mot App cua App Lab."

APP_NAME="$(basename "$(cd "$APP_DIR" && pwd)")"
REMOTE="ArduinoApps/${APP_NAME}"

say "Tim bo UNO Q"
unoq_find_host || die "Khong tim thay bo UNO Q.
     - Bo da bat va noi WiFi chua?
     - Da cai key chua: ssh-copy-id -i ~/.ssh/breezelink_vps_ed25519.pub arduino@<ip>
     - Biet dia chi thi chi dinh: UNOQ_HOST=<ip> bash \$0 $APP_DIR"
ok "${UNOQ_HOST} (${UNOQ_FACTS})"

say "Day ${APP_NAME} -> ~/${REMOTE}"
# DON RUOT, KHONG XOA THU MUC APP. Ban truoc chay `rm -rf ~/ArduinoApps/<app>`
# roi tao lai — va App Lab MAT app do:
#
#     failed to load running app: app path is not valid:
#     stat /home/arduino/ArduinoApps/Bridge-Probe: no such file or directory
#
# App Lab giu duong dan app trong trang thai cua no; thu muc bien mat mot nhip la
# no bao loi va bo luon app dang chay. `find -mindepth 1 -delete` xoa het NOI DUNG
# ma giu nguyen thu muc, nen App Lab khong bao gio thay no bien mat.
#
# Van phai xoa noi dung (khong chep de): file da bo khoi app ma con nam tren bo
# van duoc App Lab dong goi va chay — "da xoa roi ma loi cu van con" la kieu hong
# ton nhieu thoi gian nhat de tim.
#
# VAN NEN DUNG APP TRONG APP LAB TRUOC KHI DAY. Thay code ngay duoi chan mot app
# dang chay thi hanh vi khong xac dinh — no co the dang doc dung file do.
# GIU LAI `.cache` — do la thu muc build cua App Lab, khong phai cua ta. Xoa no
# lam hong lan bien dich ke tiep:
#
#     fatal error: opening dependency file
#     .../UART-Test/.cache/sketch/libraries/.../singletons.cpp.libsdetect.d:
#     No such file or directory
#
# Daemon `arduino-app-cli` chay lien tuc va giu trang thai tro vao cay thu muc do;
# xoa duoi chan no thi no khong dung lai cac thu muc con, va trinh bien dich chet
# o buoc ghi file phu thuoc. Chi don nhung gi CHINH TA day len.
$UNOQ_SSH "mkdir -p ~/${REMOTE} && find ~/${REMOTE} -mindepth 1 -maxdepth 1 ! -name .cache -exec rm -rf {} +"

TARBALL="$(mktemp -t unoq-app-XXXXXX).tgz"
trap 'rm -f "$TARBALL"' EXIT
tar czf "$TARBALL" -C "$APP_DIR" \
  --exclude='__pycache__' --exclude='*.pyc' --exclude='.venv' --exclude='.git' .
scp $UNOQ_SSH_OPTS "$TARBALL" "${UNOQ_USER}@${UNOQ_HOST}:/tmp/unoq-app.tgz" >/dev/null
$UNOQ_SSH "tar xzf /tmp/unoq-app.tgz -C ~/${REMOTE} && rm -f /tmp/unoq-app.tgz"

ok "$($UNOQ_SSH "find ~/${REMOTE} -type f | wc -l | tr -d ' '") file"

echo
say "Mo App Lab — app \"${APP_NAME}\" se co trong danh sach Apps. Bam Run."
echo "   Log o tab Python. Nho print(..., flush=True): stdout khong phai tty nen"
echo "   Python dem theo khoi, thieu flush la log hien muon hoac mat han."
