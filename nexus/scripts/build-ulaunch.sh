#!/usr/bin/env bash
# NEXUS HOME – Build des (unveränderten oder NEXUS-) uLaunch passend zur installierten Atmosphère-Version.
#
# Aufruf (WSL2/Ubuntu mit devkitPro ODER im Docker-Container devkitpro/devkita64):
#   ./nexus/scripts/build-ulaunch.sh 1.10.2            # baut Tag 1.2.3 (FW 21.2.0, unverändert)
#   ./nexus/scripts/build-ulaunch.sh 1.10.2 --branch   # baut den aktuellen Branch, Atmosphere-libs auf AMS 1.10.2 gepinnt
#
# Docker unter Windows (PowerShell, im Repo-Ordner):
#   docker run --rm -it -v ${PWD}:/src -w /src devkitpro/devkita64 bash nexus/scripts/build-ulaunch.sh 1.10.2
set -euo pipefail

AMS="${1:?Atmosphère-Version angeben, z.B. 1.10.2 (Systemeinstellungen > System zeigt 'AMS x.y.z')}"
MODE="${2:-}"

# Zuordnung laut cur-changelog.md (verifiziert 13.11.2026)
case "$AMS" in
  1.10.1) TAG=1.2.2 ;;
  1.10.2) TAG=1.2.3 ;;
  1.11.1) TAG=1.2.4 ;;
  1.11.2) TAG=1.2.5 ;;
  *) echo "Unbekannte AMS-Version $AMS – keine passende uLaunch-Version bekannt. Abbruch, nichts wird geraten." >&2; exit 1 ;;
esac

command -v aarch64-none-elf-gcc >/dev/null || { echo "devkitA64 fehlt (DEVKITPRO=${DEVKITPRO:-unset})." >&2; exit 1; }

echo "== Prüfe SDL2-Revision (uLaunch verlangt 2.28.5-3, sonst Audio-Absturz beim Suspendieren)"
(dkp-pacman -Q switch-sdl2 2>/dev/null || pacman -Q switch-sdl2 2>/dev/null || echo "switch-sdl2: nicht gefunden") | tee /dev/stderr | grep -q "2.28.5-3" \
  || echo "WARNUNG: switch-sdl2 ist nicht 2.28.5-3. Build läuft, aber nicht auf der Konsole aktivieren, bevor das geklärt ist." >&2

if [[ "$MODE" == "--branch" ]]; then
  PIN=$(git ls-tree "$TAG" libs/Atmosphere-libs | awk '{print $3}')
  echo "== Branch $(git branch --show-current), Atmosphere-libs -> $PIN (wie uLaunch $TAG)"
  git submodule update --init --recursive
  git -C libs/Atmosphere-libs fetch --depth 1 origin "$PIN" && git -C libs/Atmosphere-libs checkout -q "$PIN"
  WORK=.
else
  WORK="build-vanilla-$TAG"
  echo "== Unverändertes uLaunch $TAG nach $WORK"
  rm -rf "$WORK"
  git worktree add -f "$WORK" "$TAG"
  git -C "$WORK" submodule update --init --recursive
fi

make -C "$WORK" -j"$(nproc)"
echo "== Fertig: $WORK/SdOut  (NICHT direkt auf sysMMC kopieren – siehe nexus/docs/NEXUS-ANALYSE.md §10)"
