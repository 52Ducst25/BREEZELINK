#!/usr/bin/env bash
#
# deploy.sh — safe source-only deploy of the Aircon backend to the VPS.
#
# What it does, and only this:
#   1. compiles src/ locally so a broken build never leaves your machine
#   2. refuses to run unless docker/.env still exists ON THE SERVER
#   3. backs up the server's current src/ (timestamped) so you can roll back
#   4. ships ONLY src/ — it never transfers or deletes anything under docker/,
#      so docker/.env (the Cloudflare tunnel token) can never be clobbered
#   5. rebuilds api + worker, then re-attaches cloudflared, then health-checks
#
# Why source-only: the VPS image bakes code in (no source mount), so every code
# change needs a rebuild — but the compose files, Dockerfile and .env on the
# server are managed by hand and must not be touched by a routine deploy. The
# one time this project shipped the whole tree, an `rm -rf docker` wiped the
# tunnel token and took the site down. This script makes that impossible: it
# has no code path that writes under docker/.
#
# Usage:
#   scripts/deploy.sh                 # deploy, with a confirmation prompt
#   scripts/deploy.sh --yes           # skip the prompt (for your own automation)
#   AC_HOST=1.2.3.4 scripts/deploy.sh # override the target
#
# Auth: uses your normal ssh. Set up an SSH key for a passwordless run, or just
# type the password when ssh asks. The password is NEVER stored in this script.

set -euo pipefail

# ---- config -----------------------------------------------------------------
# KHÔNG GHI CỨNG ĐỊA CHỈ SERVER TRONG FILE ĐƯỢC COMMIT. Repo này công khai, và
# IP + user SSH + tên miền gộp lại là một tấm bản đồ khá đầy đủ cho người muốn
# thử cửa. Chúng không phải bí mật (không đăng nhập được bằng chúng), nhưng
# không có lý do gì để công bố sẵn.
#
# Ba cách nạp, xét theo thứ tự — cách đầu tiên có giá trị sẽ thắng:
#   1. biến môi trường:   AC_HOST=1.2.3.4 scripts/deploy.sh
#   2. file cấu hình:     scripts/deploy.env   (BỊ GITIGNORE — cách tiện nhất)
#   3. không có gì        -> dừng lại và nói thiếu biến nào
#
# Mẫu: scripts/deploy.env.example
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
[ -f "$SCRIPT_DIR/deploy.env" ] && . "$SCRIPT_DIR/deploy.env"

AC_REMOTE_DIR="${AC_REMOTE_DIR:-aircon}"          # relative to the user's home
AC_PROJECT="${AC_PROJECT:-aircon}"                # docker compose -p
AC_COMPOSE="${AC_COMPOSE:-docker/docker-compose.vps.yml}"
SSH_OPTS="-o ConnectTimeout=30"

# Ba giá trị KHÔNG có mặc định — chúng gắn với một máy chủ cụ thể.
# Dừng NGAY ở đây chứ không để script chạy tới bước ssh rồi treo ở một tên máy
# rỗng: lỗi lúc đó đọc ra là "mạng hỏng", không phải "thiếu cấu hình".
: "${AC_HOST:?thiếu AC_HOST — tạo scripts/deploy.env từ deploy.env.example}"
: "${AC_USER:?thiếu AC_USER — tạo scripts/deploy.env từ deploy.env.example}"
: "${AC_URL:?thiếu AC_URL — tạo scripts/deploy.env từ deploy.env.example}"

# ---- locate the repo root, no matter where the script is called from --------
# SCRIPT_DIR đã tính ở khối cấu hình bên trên (nó cần để nạp deploy.env).
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

SSH="ssh $SSH_OPTS ${AC_USER}@${AC_HOST}"
REMOTE="\$HOME/${AC_REMOTE_DIR}"

say()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

SKIP_PROMPT=0
[ "${1:-}" = "--yes" ] && SKIP_PROMPT=1

# ---- 0. sanity: are we actually in the repo? --------------------------------
[ -d src/app ] || die "src/app not found — run this from the Aircon repo."
[ -f "$AC_COMPOSE" ] || die "$AC_COMPOSE not found."

# ---- 1. compile locally: catch a syntax error before it ships ---------------
say "Kiểm tra cú pháp src/ trên máy local"
PY="$(command -v python || command -v python3 || true)"
if [ -n "$PY" ]; then
  "$PY" -m compileall -q src/app || die "src/ có lỗi cú pháp — sửa xong hãy deploy."
  ok "cú pháp sạch"
else
  say "  (không tìm thấy python trên máy — bỏ qua bước kiểm cú pháp)"
fi

# ---- 2. HARD GUARD: the tunnel token must exist on the server ---------------
# This is the failure this whole script exists to prevent. If .env is already
# gone, stop now and say so loudly rather than deploying onto a broken tunnel.
say "Kiểm tra docker/.env còn trên server (token Cloudflare)"
if ! $SSH "test -f ${REMOTE}/docker/.env"; then
  die "docker/.env KHÔNG tồn tại trên server. Dừng lại.
     Khôi phục từ backup trước khi deploy, ví dụ:
       ssh ${AC_USER}@${AC_HOST} 'cp ~/aircon.bak.*/docker/.env ~/${AC_REMOTE_DIR}/docker/.env'"
fi
ok "docker/.env còn nguyên — sẽ không đụng tới nó"

# ---- 3. confirmation --------------------------------------------------------
echo
say "Sắp deploy:"
echo "     từ:   $REPO_ROOT/src"
echo "     tới:  ${AC_USER}@${AC_HOST}:${AC_REMOTE_DIR}/src"
echo "     build lại: api + worker  (docker/ trên server KHÔNG bị đụng)"
echo
if [ "$SKIP_PROMPT" -eq 0 ]; then
  read -r -p "Tiếp tục? [y/N] " reply < /dev/tty
  case "$reply" in
    y|Y|yes|YES) ;;
    *) die "Đã hủy." ;;
  esac
fi

# ---- 4. package src/ only ---------------------------------------------------
STAMP="$(date +%Y%m%d-%H%M%S)"
TARBALL="$(mktemp -t aircon-src-XXXXXX).tgz"
trap 'rm -f "$TARBALL"' EXIT
say "Đóng gói src/ (bỏ __pycache__, *.pyc, egg-info)"
tar czf "$TARBALL" \
  --exclude='__pycache__' --exclude='*.pyc' --exclude='*.egg-info' \
  src
ok "$(du -h "$TARBALL" | cut -f1) gói xong"

# ---- 5. ship + swap (src only; docker/ untouched) ---------------------------
say "Chuyển lên server"
scp $SSH_OPTS "$TARBALL" "${AC_USER}@${AC_HOST}:/tmp/aircon-src.tgz"

say "Sao lưu src/ cũ + thay src/ mới trên server"
# One remote block. The backup lets you roll back; the swap replaces src/
# wholesale so deleted files are really gone. docker/ is never named here.
$SSH "set -e
  cd ${REMOTE}
  [ -d src ] && cp -r src .src.bak.${STAMP}
  rm -rf src
  tar xzf /tmp/aircon-src.tgz -C ${REMOTE}
  rm -f /tmp/aircon-src.tgz
  # keep only the 3 most recent backups
  ls -dt .src.bak.* 2>/dev/null | tail -n +4 | xargs -r rm -rf
  echo '  backup: .src.bak.${STAMP}'"
ok "src/ đã thay"

# ---- 6. rebuild ------------------------------------------------------------
# api + worker share the image and run migrations on api start. cloudflared is
# recreated LAST and on its own: it shares api's network namespace
# (network_mode: service:api), so rebuilding api detaches it and it must be
# recreated to re-attach — otherwise the public URL 502s.
say "Build lại api + worker (alembic upgrade head tự chạy khi api khởi động)"
$SSH "cd ${REMOTE} && docker compose -p ${AC_PROJECT} -f ${AC_COMPOSE} up -d --build api worker" \
  || die "docker compose build thất bại. src/ cũ ở .src.bak.${STAMP} trên server."

say "Đợi api khỏe rồi gắn lại cloudflared"
$SSH "cd ${REMOTE}
  for i in \$(seq 1 20); do
    s=\$(docker inspect -f '{{.State.Health.Status}}' ${AC_PROJECT}-api-1 2>/dev/null || echo none)
    [ \"\$s\" = healthy ] && break
    sleep 3
  done
  docker compose -p ${AC_PROJECT} -f ${AC_COMPOSE} up -d cloudflared"

# ---- 7. verify -------------------------------------------------------------
say "Kiểm tra"
$SSH "docker ps --filter name=${AC_PROJECT} --format '  {{.Names}}: {{.Status}}'"
REV="$($SSH "docker exec ${AC_PROJECT}-postgres-1 psql -U ${AC_PROJECT} -d ${AC_PROJECT} -t -c 'select version_num from alembic_version;' 2>/dev/null | tr -d ' \n' || true")"
[ -n "$REV" ] && ok "alembic revision: $REV"

# CHO TUNNEL GAN LAI, dung hoi mot lan roi ket luan.
#
# Buoc 6 da cho `api` bao healthy roi moi dung lai cloudflared, nhung `healthy`
# noi ve container api, khong noi ve duong hop le tu Cloudflare vao no. Tunnel
# can them ~15-30 giay de bat tay lai. Hoi mot lan ngay sau do gan nhu chac chan
# gap 502, va script bao DEPLOY THAT BAI cho mot lan deploy da thanh cong —
# nguoi doc se di rollback mot ban hoan toan lanh lan.
CODE=000
for i in $(seq 1 12); do
  CODE="$(curl -s -m 10 -o /dev/null -w '%{http_code}' "${AC_URL}/web/login" || echo 000)"
  [ "$CODE" = "200" ] && break
  sleep 5
done
if [ "$CODE" = "200" ]; then
  ok "${AC_URL}/web/login -> 200"
else
  die "${AC_URL}/web/login -> ${CODE}. Kiểm tra: ssh ${AC_USER}@${AC_HOST} 'docker logs ${AC_PROJECT}-api-1 --tail 40'
     Rollback src/: ssh ${AC_USER}@${AC_HOST} 'cd ${AC_REMOTE_DIR} && rm -rf src && cp -r .src.bak.${STAMP} src && docker compose -p ${AC_PROJECT} -f ${AC_COMPOSE} up -d --build api worker'"
fi

echo
say "Xong. Deploy thành công."
