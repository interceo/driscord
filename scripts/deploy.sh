#!/usr/bin/env sh
# Build the Driscord API and signaling images in-cluster (Kaniko), apply the
# manifests and roll both deployments out to k3s. Kaniko builds from this repo
# clone on the node (/mnt/raid1/homelab/repos/driscord), so push your changes
# there first.
#
# Prereqs: kubectl context set; SOPS age key at ~/.config/sops/age/keys.txt
# (secret.yaml is rendered from secret.sops.yaml at apply time, then shredded).
#
# Usage: scripts/deploy.sh
set -eu

NS=driscord
MANIFESTS=/homelab/apps/driscord
APPLY=/homelab/system/scripts/apply.sh

build_image() {
    job=$1
    manifest=$2

    echo ">> building ${job} image (Kaniko)..."
    kubectl delete "job/${job}" -n "$NS" --ignore-not-found
    kubectl apply -f "$MANIFESTS/$manifest"
    if ! kubectl wait --for=condition=complete "job/${job}" -n "$NS" --timeout=1800s; then
        echo "!! ${job} build failed -- logs:" >&2
        kubectl logs -n "$NS" -l "job-name=${job}" --tail=40 >&2
        exit 1
    fi
}

build_image driscord-api-build api-build-job.yaml
build_image driscord-signaling-build signaling-build-job.yaml

echo ">> applying manifests and rolling out..."
"$APPLY" "$MANIFESTS/"

for deployment in driscord-api driscord-signaling; do
    kubectl rollout restart "deployment/${deployment}" -n "$NS"
done

for deployment in driscord-api driscord-signaling; do
    kubectl rollout status "deployment/${deployment}" -n "$NS" --timeout=180s
done

echo ">> done."
