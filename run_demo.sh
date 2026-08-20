#!/usr/bin/env bash
set -euo pipefail

IMAGE_NAME="${IMAGE_NAME:-safety-critical-ha:phase0}"
EXPECTED_ENTRYPOINT='["/usr/local/bin/safety-critical-ha"]'

docker build --pull --progress=plain --tag "${IMAGE_NAME}" .
docker run --rm "${IMAGE_NAME}" --version
ENTRYPOINT_JSON="$(docker image inspect "${IMAGE_NAME}" --format '{{json .Config.Entrypoint}}')"

if [[ "${ENTRYPOINT_JSON}" != "${EXPECTED_ENTRYPOINT}" ]]; then
	echo "Entrypoint check failed for ${IMAGE_NAME}" >&2
	echo "Expected: ${EXPECTED_ENTRYPOINT}" >&2
	echo "Actual:   ${ENTRYPOINT_JSON}" >&2
	exit 1
fi

echo "Smoke test passed: ${IMAGE_NAME}"