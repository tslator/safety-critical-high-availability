#!/usr/bin/env bash
set -Eeuo pipefail

cd "$(dirname "$0")"

cleanup() {
  status=$?
  echo "==> cleaning up demo stack"
  docker compose down --volumes --remove-orphans || true
  exit "$status"
}
trap cleanup EXIT INT TERM

echo "==> validating compose configuration"
docker compose config --quiet

echo "==> building stack"
docker compose build

echo "==> starting default services"
docker compose up --wait --no-build

echo "==> service status"
docker compose ps

echo "==> runtime smoke check"
docker compose exec supervisor safety-critical-ha --version