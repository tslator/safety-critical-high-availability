Gate: G0.3
Date: 2026-08-20
|Command|Result|
|---|---|
|`docker build --pull --progress=plain --tag safety-critical-ha:phase0 .`|PASS (builder CTest succeeded)|
|`docker run --rm safety-critical-ha:phase0 --version`|PASS|
|`docker image inspect safety-critical-ha:phase0 --format '{{json .Config.Entrypoint}}'`|["/usr/local/bin/safety-critical-ha"]|
|`docker image inspect safety-critical-ha:phase0 --format '{{.Size}}'`|124498471|

---

