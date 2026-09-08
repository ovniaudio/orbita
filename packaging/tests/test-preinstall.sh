#!/usr/bin/env bash
# packaging/tests/test-preinstall.sh — el preinstall del .pkg retira SOLO nuestro ORBIT viejo.
#
# Por qué existe: en 0.3.0 el bundle pasa a llamarse "OVNI ORBIT" (D-32). Si el instalador no
# retirara el "ORBIT.component"/"ORBIT.vst3" de 0.2.x, el usuario quedaría con DOS plugins que
# declaran la misma identidad de AU (aufx/Orbt/Ovni) y el DAW mostraría un duplicado.
# Y el motivo de todo el rename es que hay OTROS "Orbit" allá afuera (Phantom Sounds): tocar
# uno ajeno sería exactamente el bug que estamos arreglando, pero al revés y peor.
#
# Se ejercita sobre un ÁRBOL FALSO (nada real se toca), con los tres casos que importan:
#   · bundle NUESTRO   (CFBundleIdentifier = com.ovni.orbit) → se retira
#   · bundle AJENO     (mismo nombre, otro id)               → NO se toca
#   · no hay nada                                            → sale limpio, sin crear basura
# y en los dos dominios (sistema y usuario).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="$ROOT/packaging/scripts/preinstall"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fails=0
ok()  { printf '  preinstall: ✓ %s\n' "$*"; }
bad() { printf '  preinstall: ✗ %s\n' "$*"; fails=$((fails+1)); }

# Un bundle mínimo con su Info.plist (el preinstall decide MIRANDO el CFBundleIdentifier).
make_bundle() { # $1 = ruta · $2 = CFBundleIdentifier · $3 = nombre
  mkdir -p "$1/Contents/MacOS"
  printf 'no soy un binario\n' > "$1/Contents/MacOS/$3"
  cat > "$1/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>$3</string>
  <key>CFBundleIdentifier</key><string>$2</string>
  <key>CFBundleName</key><string>$3</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>0.2.1</string>
</dict></plist>
PLIST
}

# Corre el preinstall como lo llama Installer.app: $1 pkg · $2 destino · $3 VOLUMEN · $4 raíz.
run_preinstall() { # $1 = volumen falso · $2 = home falso → log en $WORK/log
  ORBIT_PREINSTALL_HOME="$2" "$SCRIPT" /fake.pkg "$1" "$1" "$1" > "$WORK/log" 2>&1
}

retired_count() { # $1 = raíz del dominio → cuántos bundles hay en el cajón de retirados
  find "$1/Library/Application Support/OVNI Audio/replaced" -maxdepth 2 -mindepth 2 \
       \( -name '*.vst3' -o -name '*.component' \) 2>/dev/null | wc -l | tr -d ' '
}

#--------------------------------------------------------------------------------------------------
# Caso 1 — nuestro ORBIT viejo en el dominio SISTEMA: se retira (VST3 y AU).
#--------------------------------------------------------------------------------------------------
V="$WORK/c1/vol"; H="$WORK/c1/home"; mkdir -p "$H"
make_bundle "$V/Library/Audio/Plug-Ins/VST3/ORBIT.vst3"           com.ovni.orbit ORBIT
make_bundle "$V/Library/Audio/Plug-Ins/Components/ORBIT.component" com.ovni.orbit ORBIT
run_preinstall "$V" "$H"; rc=$?
[ "$rc" = 0 ] && ok "caso 1: sale 0" || bad "caso 1: salió $rc — $(cat "$WORK/log")"
[ ! -e "$V/Library/Audio/Plug-Ins/VST3/ORBIT.vst3" ]            && ok "caso 1: el VST3 viejo ya no está en VST3/" || bad "caso 1: el VST3 viejo sigue ahí"
[ ! -e "$V/Library/Audio/Plug-Ins/Components/ORBIT.component" ]  && ok "caso 1: el AU viejo ya no está en Components/" || bad "caso 1: el AU viejo sigue ahí"
[ "$(retired_count "$V")" = 2 ]                                  && ok "caso 1: los 2 quedaron guardados, no borrados" || bad "caso 1: en el cajón hay $(retired_count "$V") (esperaba 2)"

#--------------------------------------------------------------------------------------------------
# Caso 2 — un "Orbit" AJENO con el mismo nombre de archivo: NO se toca. (El motivo del rename.)
#--------------------------------------------------------------------------------------------------
V="$WORK/c2/vol"; H="$WORK/c2/home"; mkdir -p "$H"
make_bundle "$V/Library/Audio/Plug-Ins/Components/ORBIT.component" com.phantomsounds.orbit ORBIT
sum_antes="$(shasum "$V/Library/Audio/Plug-Ins/Components/ORBIT.component/Contents/Info.plist" | awk '{print $1}')"
run_preinstall "$V" "$H"; rc=$?
[ "$rc" = 0 ] && ok "caso 2: sale 0" || bad "caso 2: salió $rc"
if [ -e "$V/Library/Audio/Plug-Ins/Components/ORBIT.component" ] \
   && [ "$(shasum "$V/Library/Audio/Plug-Ins/Components/ORBIT.component/Contents/Info.plist" | awk '{print $1}')" = "$sum_antes" ]; then
  ok "caso 2: el bundle ajeno quedó intacto"
else
  bad "caso 2: TOCAMOS un bundle que no es nuestro"
fi
[ "$(retired_count "$V")" = 0 ] && ok "caso 2: no se retiró nada" || bad "caso 2: se retiró algo ajeno"

#--------------------------------------------------------------------------------------------------
# Caso 3 — no hay ningún ORBIT: sale 0 y no deja basura.
#--------------------------------------------------------------------------------------------------
V="$WORK/c3/vol"; H="$WORK/c3/home"; mkdir -p "$V/Library/Audio/Plug-Ins/VST3" "$H"
run_preinstall "$V" "$H"; rc=$?
[ "$rc" = 0 ] && ok "caso 3: sale 0 con el disco vacío" || bad "caso 3: salió $rc"
[ ! -d "$V/Library/Application Support/OVNI Audio/replaced" ] && ok "caso 3: no creó el cajón sin necesidad" || bad "caso 3: creó carpetas de más"

#--------------------------------------------------------------------------------------------------
# Caso 4 — el ORBIT viejo instalado en la carpeta del USUARIO (el workaround que se le dio a la gente).
#--------------------------------------------------------------------------------------------------
V="$WORK/c4/vol"; H="$WORK/c4/home"; mkdir -p "$V"
make_bundle "$H/Library/Audio/Plug-Ins/Components/ORBIT.component" com.ovni.orbit ORBIT
make_bundle "$H/Library/Audio/Plug-Ins/VST3/ORBIT.vst3"            com.phantomsounds.orbit ORBIT
run_preinstall "$V" "$H"; rc=$?
[ "$rc" = 0 ] && ok "caso 4: sale 0" || bad "caso 4: salió $rc"
[ ! -e "$H/Library/Audio/Plug-Ins/Components/ORBIT.component" ] && ok "caso 4: retiró el nuestro del home" || bad "caso 4: no retiró el nuestro del home"
[ -e "$H/Library/Audio/Plug-Ins/VST3/ORBIT.vst3" ]              && ok "caso 4: dejó el ajeno del home" || bad "caso 4: se llevó puesto el ajeno del home"
[ "$(retired_count "$H")" = 1 ]                                  && ok "caso 4: 1 en el cajón del usuario" || bad "caso 4: en el cajón hay $(retired_count "$H") (esperaba 1)"

#--------------------------------------------------------------------------------------------------
# Caso 5 — un ORBIT.component SIN Info.plist legible: se deja en paz (no sabemos de quién es).
#--------------------------------------------------------------------------------------------------
V="$WORK/c5/vol"; H="$WORK/c5/home"; mkdir -p "$H" "$V/Library/Audio/Plug-Ins/Components/ORBIT.component/Contents"
run_preinstall "$V" "$H"; rc=$?
[ "$rc" = 0 ] && ok "caso 5: sale 0" || bad "caso 5: salió $rc"
[ -e "$V/Library/Audio/Plug-Ins/Components/ORBIT.component" ] && ok "caso 5: sin Info.plist no se toca" || bad "caso 5: retiró un bundle que no pudo identificar"

#--------------------------------------------------------------------------------------------------
# Caso 6 — "OVNI Audio" del home es un SYMLINK del usuario: no se sigue (el script corre como root).
#          El bundle viejo se queda donde esta, no se escribe nada del otro lado del enlace, sale 0.
#--------------------------------------------------------------------------------------------------
V="$WORK/c6/vol"; H="$WORK/c6/home"; T="$WORK/c6/objetivo-ajeno"; mkdir -p "$V" "$H/Library/Application Support" "$T"
ln -s "$T" "$H/Library/Application Support/OVNI Audio"
make_bundle "$H/Library/Audio/Plug-Ins/Components/ORBIT.component" com.ovni.orbit ORBIT
run_preinstall "$V" "$H"; rc=$?
[ "$rc" = 0 ] && ok "caso 6: sale 0" || bad "caso 6: salió $rc"
[ -e "$H/Library/Audio/Plug-Ins/Components/ORBIT.component" ] && ok "caso 6: con un symlink en el destino, el bundle se queda donde está" || bad "caso 6: movió el bundle a través de un symlink"
[ -z "$(find "$T" -mindepth 1 2>/dev/null)" ] && ok "caso 6: no escribió nada del otro lado del enlace" || bad "caso 6: escribió a través del enlace: $(find "$T" -mindepth 1)"
grep -q "enlace simbolico" "$WORK/log" && ok "caso 6: lo avisa en el log" || bad "caso 6: no avisó"

echo
if [ "$fails" = 0 ]; then echo "  preinstall: TODO OK"; exit 0; fi
echo "  preinstall: $fails comprobacion(es) fallaron"; exit 1
