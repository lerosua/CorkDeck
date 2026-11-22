#!/bin/sh
# Run Linux host fixtures with their dependencies on Apple Silicon or Linux.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ "$(uname -s)" = Darwin ]; then
    image=${CORKDECK_HOST_IMAGE:-corkdeck-menu-host:2026.10.12}
    if ! docker image inspect "$image" >/dev/null 2>&1; then
        docker build --platform linux/arm64 -t "$image" \
            -f "$repo/horizon-wine/tests/Dockerfile.host-tests" "$repo/horizon-wine/tests"
    fi
    exec docker run --rm --platform linux/arm64 -v "$repo:/work" -w /work \
        "$image" sh horizon-wine/tests/run-host-checks.sh "$@"
fi
cd "$repo"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
export LAUNCHER_FONT="$repo/horizon-wine/assets/fonts/CorkDeckUI.ttf"
[ "$#" -gt 0 ] || set -- steam ui shell launcher cdn
for suite do
    case "$suite" in
        steam) script=check-steam-host.sh ;;
        ui) script=check-steam-ui-host.sh ;;
        shell) script=check-steam-shell-host.sh ;;
        launcher) script=check-launcher-host.sh ;;
        cdn) script=check-steam-cdn-host.sh ;;
        *) echo "Unknown host suite: $suite" >&2; exit 2 ;;
    esac
    echo "Running $suite host checks"
    sh "horizon-wine/tests/$script"
done
