#!/usr/bin/env bash
# Generate a self-signed RSA TLS certificate with SANs (openssl).
# Use for lab / authorized testing. Browsers will still warn unless you trust the cert.
#
# Usage:
#   ./scripts/gen_teamserver_disguise_tls.sh [output_dir]
# Env (optional):
#   TLS_CN   — Common Name (default: random gateway-style host, no .example TLD)
#   TLS_ORG  — Organization (default: random generic corporate name)
#   TLS_OU   — Organizational Unit (default: random generic OU)
#   TLS_CC   — Country code (default: random US/CN/SG/DE/GB/JP)
#   TLS_DAYS — Validity in days (default: 397)
#   TLS_EXTRA_SANS — space-separated extra DNS names

set -euo pipefail

OUT_DIR="${1:-.}"
mkdir -p "$OUT_DIR"

CN="${TLS_CN:-}"
if [[ -z "$CN" ]]; then
  RND="$(openssl rand -hex 4)"
  case $((RANDOM % 6)) in
    0) CN="www.cdn-${RND}.edge-gateway.net" ;;
    1) CN="api.${RND}.static-delivery.net" ;;
    2) CN="gateway-${RND}.https-accelerate.net" ;;
    3) CN="origin-${RND}.traffic-route.net" ;;
    4) CN="edge-${RND}.cdn-platform.net" ;;
    *) CN="static-${RND}.asset-delivery.net" ;;
  esac
fi

if [[ -n "${TLS_ORG:-}" ]]; then
  ORG="$TLS_ORG"
else
  ORGS=(
    "Cloud Networking Technology Co., Ltd."
    "Digital Infrastructure Holdings"
    "Application Delivery Services Inc."
    "Internet Traffic Engineering Ltd."
    "Hosted Platform Solutions"
    "Global Edge Computing Corp"
    "Enterprise Web Services Group"
  )
  ORG="${ORGS[$((RANDOM % ${#ORGS[@]}))]}"
fi

if [[ -n "${TLS_OU:-}" ]]; then
  OU="$TLS_OU"
else
  OUS=(
    "IT Operations"
    "Public HTTPS Services"
    "API Gateway Division"
    "Web Acceleration Team"
    "Security and Compliance"
    "Platform Engineering"
  )
  OU="${OUS[$((RANDOM % ${#OUS[@]}))]}"
fi

CC="${TLS_CC:-}"
if [[ -z "$CC" ]]; then
  CCS=(US CN SG DE GB JP)
  CC="${CCS[$((RANDOM % ${#CCS[@]}))]}"
fi

DAYS="${TLS_DAYS:-397}"

KEY="$OUT_DIR/server.disguise.key"
CRT="$OUT_DIR/server.disguise.crt"
CFG="$(mktemp)"
trap 'rm -f "$CFG"' EXIT

{
  echo "[req]"
  echo "default_bits = 2048"
  echo "prompt = no"
  echo "default_md = sha256"
  echo "distinguished_name = dn"
  echo "req_extensions = v3_req"
  echo "[dn]"
  echo "C = $CC"
  echo "O = $ORG"
  echo "OU = $OU"
  echo "CN = $CN"
  echo "[v3_req]"
  echo "basicConstraints = CA:FALSE"
  echo "keyUsage = digitalSignature, keyEncipherment"
  echo "extendedKeyUsage = serverAuth"
  echo "subjectAltName = @alt_names"
  echo "[alt_names]"
  echo "DNS.1 = $CN"
  echo "DNS.2 = localhost"
  i=3
  for extra in ${TLS_EXTRA_SANS:-}; do
    [[ -z "$extra" ]] && continue
    echo "DNS.$i = $extra"
    i=$((i + 1))
  done
} >"$CFG"

openssl req -x509 -nodes -newkey rsa:2048 \
  -keyout "$KEY" -out "$CRT" \
  -days "$DAYS" \
  -config "$CFG" -extensions v3_req

chmod 600 "$KEY"
chmod 644 "$CRT"

echo "Wrote: $CRT"
echo "       $KEY"
echo "Point profile Teamserver.cert / Teamserver.key to these files (or copy names to server.rsa.*)."
