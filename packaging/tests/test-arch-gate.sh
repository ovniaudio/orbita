#!/usr/bin/env bash
# packaging/tests/test-arch-gate.sh — el .pkg de ORBIT NO puede salir con un payload que no sea universal.
#
# Por que existe: en SUPERNOVA 0.3.0 el .pkg estuvo a un pelo de salir arm64-only porque el build/
# habia quedado configurado para la arquitectura nativa, y NADIE lo hubiera atrapado -- el
# empaquetador arma el .pkg igual de contento con un payload thin, y el problema recien aparece en
# una Mac Intel, DESPUES de publicar. ORBIT corre el mismo riesgo: este repo tiene tres carpetas de
# build y solo una esta configurada como universal (CMAKE_OSX_ARCHITECTURES="arm64;x86_64").
#
# Se arman bundles de mentira (Info.plist + un ejecutable de VERDAD hecho con clang) y se comprueban
# las tres caras:
#   · arm64-only            → el empaquetado tiene que FALLAR, y decir por que
#   · universal             → tiene que PASAR (si no, la guardia seria un "siempre falla")
#   · bundle sin ejecutable → tiene que FALLAR (un .component vacio colgado del .vst3 que si trae)
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="$ROOT/packaging/make-orbit-pkg.sh"
VERSION="9.9.9"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fails=0
ok()  { printf '  arch-gate: ✓ %s\n' "$*"; }
bad() { printf '  arch-gate: ✗ %s\n' "$*"; fails=$((fails+1)); }

make_bundle() { # $1 = ruta del bundle · $2 = nombre del ejecutable · $3... = flags de arch para clang
  local dir="$1" name="$2"; shift 2
  mkdir -p "$dir/Contents/MacOS"
  cat > "$dir/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>$name</string>
  <key>CFBundleIdentifier</key><string>com.ovni.orbit</string>
  <key>CFBundleName</key><string>$name</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
</dict></plist>
PLIST
  if [ "$#" -gt 0 ]; then
    # Archivo de verdad, no stdin: clang no arma un universal (varios -arch) leyendo de "-".
    local src="$WORK/fake.c"
    [ -f "$src" ] || echo 'int main(void){return 0;}' > "$src"
    clang "$@" -o "$dir/Contents/MacOS/$name" "$src" 2>/dev/null \
      || { echo "  arch-gate: no pude compilar el ejecutable de mentira ($*)" >&2; exit 2; }
  fi
}

run_pkg() { # $1 = subdir → log en $WORK/$1/log
  mkdir -p "$WORK/$1/out"
  ART_DIR=/nonexistent INSTALLER_SIGN_ID= \
    "$SCRIPT" --version "$VERSION" --bundles "$WORK/$1/bundles" --outdir "$WORK/$1/out" \
    > "$WORK/$1/log" 2>&1
}

#------------------------------------------------------------------- caso 1: arm64-only → FALLA
mkdir -p "$WORK/thin/bundles"
make_bundle "$WORK/thin/bundles/OVNI ORBIT.vst3"      "OVNI ORBIT" -arch arm64
make_bundle "$WORK/thin/bundles/OVNI ORBIT.component" "OVNI ORBIT" -arch arm64
if run_pkg thin; then
  bad "caso 1: empaqueto un payload arm64-only (tendria que haber fallado)"
else
  ok "caso 1: arm64-only rechazado"
  grep -qi "universal" "$WORK/thin/log" && ok "caso 1: el error dice que no es universal" \
    || bad "caso 1: fallo, pero el mensaje no explica que el problema es la arquitectura"
fi
[ -f "$WORK/thin/out/OVNI-ORBIT-v$VERSION.pkg" ] && bad "caso 1: dejo el .pkg thin en el outdir" \
  || ok "caso 1: no quedo ningun .pkg thin en el outdir"

#------------------------------------------------------------------- caso 2: universal → PASA
mkdir -p "$WORK/uni/bundles"
make_bundle "$WORK/uni/bundles/OVNI ORBIT.vst3"      "OVNI ORBIT" -arch arm64 -arch x86_64
make_bundle "$WORK/uni/bundles/OVNI ORBIT.component" "OVNI ORBIT" -arch arm64 -arch x86_64
if run_pkg uni; then
  ok "caso 2: universal aceptado"
  [ -f "$WORK/uni/out/OVNI-ORBIT-v$VERSION.pkg" ] && ok "caso 2: emitio el .pkg" || bad "caso 2: no emitio el .pkg"
else
  bad "caso 2: rechazo un payload universal — la guardia es un 'siempre falla': $(tail -3 "$WORK/uni/log")"
fi

#------------------------------------------------------------------- caso 3: un bundle sin ejecutable → FALLA
mkdir -p "$WORK/empty/bundles"
make_bundle "$WORK/empty/bundles/OVNI ORBIT.vst3"      "OVNI ORBIT" -arch arm64 -arch x86_64
make_bundle "$WORK/empty/bundles/OVNI ORBIT.component" "OVNI ORBIT"   # sin binario
if run_pkg empty; then
  bad "caso 3: empaqueto un bundle sin ejecutable"
else
  ok "caso 3: bundle sin ejecutable rechazado"
fi

echo
if [ "$fails" = 0 ]; then echo "  arch-gate: TODO OK"; exit 0; fi
echo "  arch-gate: $fails comprobacion(es) fallaron"; exit 1
