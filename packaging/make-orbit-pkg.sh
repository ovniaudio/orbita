#!/usr/bin/env bash
# packaging/make-orbit-pkg.sh — arma el instalador macOS de ORBIT (OVNI-ORBIT-v<X.Y.Z>.pkg).
#
# Emite, desde una carpeta con los dos bundles ya construidos ("OVNI ORBIT.vst3" + "OVNI ORBIT.component"):
#   · OVNI-ORBIT-v<X.Y.Z>.pkg   instalador de doble click (VST3 + AU en /Library/Audio/Plug-Ins)
#   · SHA256SUMS.txt            checksum de lo emitido
#
# DE DONDE SALE. Es la version propia de ORBIT del make-per-plugin.sh del catalogo (el que armo el
# .pkg de v0.2.1 el 3-sep, ver mision-control/prompts/20b y 20c). Se trae al repo publico porque
# ORBIT se construye, commitea y PUBLICA desde aca (D-16): el instalador que baja la gente tiene que
# poder rehacerse con lo que hay en este arbol, sin depender de otro repo. Conserva sus tres defensas
# y agrega las dos cosas que ORBIT necesita en 0.3.0 y el del catalogo no sabe hacer: el preinstall
# que retira el bundle viejo (D-32) y un SOURCE.txt que apunta al arbol EXACTO de esta version.
#
# LAS DEFENSAS (por que son como son)
#   1. GUARDIA DE VERSION, antes de empaquetar. El .pkg de ORBIT v0.2.0 dejo a la gente sin el AU:
#      los bundles se habian construido sin bumpear VERSION, declaraban 0.1.1, y macOS Installer
#      comparaba <bundle-version> con lo instalado y SALTEABA el componente. Si un bundle no declara
#      exactamente --version, esto falla acá, con el bundle en la mano.
#      (mision-control/bugs/2026-09-03-orbit-au-no-se-instala.md)
#   2. BundleIsVersionChecked=false + BundleOverwriteAction=upgrade en cada component plist: el
#      Installer deja de comparar versiones y SIEMPRE escribe el bundle. BundleIsRelocatable=false:
#      si el usuario tiene una copia vieja en ~/Library (instalacion manual del DMG), el Installer
#      NO debe "relocalizar" la nueva ahi — siempre /Library.
#   3. POST-CHECK sobre el .pkg YA EMITIDO: se expande y se exige que todo lo que declara una
#      version declare la pedida. Es lo unico que mira lo que de verdad se distribuye.
#
# IDENTIFICADORES. Se fijan a com.ovni.plugins.orbit.{vst3,au} y com.ovni.plugins.license — los
# MISMOS que emitio v0.2.1 — para que los recibos de pkgutil sigan siendo coherentes al actualizar.
# NO se derivan del nombre visible: ese ahora es "OVNI ORBIT" y ensuciaria los ids.
#
# Cumplimiento AGPLv3 (§4/§6): el instalador muestra la licencia e instala LICENSE.txt + NOTICE.txt +
# SOURCE.txt en /Library/Audio/Plug-Ins/OVNI Audio/.
#
# Uso:
#   packaging/make-orbit-pkg.sh --bundles <dir> --outdir <dir> [--version X.Y.Z] [--license f] [--notice f]
#   (--version por defecto sale de ./VERSION)
#
# Firma: INSTALLER_SIGN_ID opcional. Vacio => .pkg SIN FIRMAR (camino gratis; no falla). La firma y la
# notarizacion las corre Joaquin con su llavero, en un paso aparte, sobre este mismo script.
set -uo pipefail

log()  { printf '  make-orbit-pkg: %s\n' "$*" >&2; }
fail() { printf '  make-orbit-pkg: ERROR: %s\n' "$*" >&2; exit 1; }

plist_set() { # $1 = plist · $2 = clave · $3 = tipo · $4 = valor
  /usr/libexec/PlistBuddy -c "Set :$2 $4" "$1" >/dev/null 2>&1 && return 0
  /usr/libexec/PlistBuddy -c "Add :$2 $3 $4" "$1" >/dev/null 2>&1 && return 0
  fail "no pude escribir $2=$4 en $(basename "$1")"
}

bundle_version() { /usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$1/Contents/Info.plist" 2>/dev/null; }

# Valores de un atributo XML, uno por linea (aplana el XML a una etiqueta por linea primero).
xml_attr() { tr '\n' ' ' | tr '<' '\n' | grep "^$1[ />]" | sed -n "s/.*[[:space:]]$2=\"\([^\"]*\)\".*/\1/p"; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALLER_SIGN_ID="${INSTALLER_SIGN_ID:-}"
PKG_ID="com.ovni.plugins"
SLUG="orbit"                       # identificadores y nombres de componente: NO siguen al nombre visible
TITLE_NAME="ORBIT"                 # lo que dice la ventana del instalador (igual que en v0.2.1)
ART_DIR="${ART_DIR:-$ROOT/packaging/installer-resources}"
PREINSTALL="${PREINSTALL:-$ROOT/packaging/scripts/preinstall}"

VERSION=""; BUNDLES=""; OUTDIR=""
LICENSE_FILE="${LICENSE_FILE:-$ROOT/LICENSE}"
NOTICE_FILE="${NOTICE_FILE:-$ROOT/NOTICE.md}"
while [ "$#" -gt 0 ]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2 ;;
    --bundles) BUNDLES="${2:-}"; shift 2 ;;
    --outdir)  OUTDIR="${2:-}"; shift 2 ;;
    --license) LICENSE_FILE="${2:-}"; shift 2 ;;
    --notice)  NOTICE_FILE="${2:-}"; shift 2 ;;
    *) fail "argumento desconocido: $1" ;;
  esac
done
[ -n "$VERSION" ] || VERSION="$(tr -d ' \t\r\n' < "$ROOT/VERSION" 2>/dev/null)"
[ -n "$VERSION" ] || fail "falta --version y no pude leer $ROOT/VERSION"
[ -d "${BUNDLES:-}" ] || fail "falta --bundles <dir>"
[ -n "$OUTDIR" ] || fail "falta --outdir"
[ -f "$LICENSE_FILE" ] || fail "no encuentro LICENSE en $LICENSE_FILE (AGPLv3 §4)"
[ -f "$NOTICE_FILE" ]  || fail "no encuentro NOTICE en $NOTICE_FILE (atribucion de CIPIC)"
[ -x "$PREINSTALL" ]   || fail "no encuentro el preinstall ejecutable en $PREINSTALL"
command -v pkgbuild     >/dev/null 2>&1 || fail "no encuentro pkgbuild (¿macOS?)"
command -v productbuild >/dev/null 2>&1 || fail "no encuentro productbuild (¿macOS?)"
mkdir -p "$OUTDIR"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# --- Descubrir el par de bundles (el nombre visible puede cambiar; los ids no). ---
NAME=""
for v in "$BUNDLES"/*.vst3; do
  [ -e "$v" ] || continue
  [ -z "$NAME" ] || fail "hay mas de un .vst3 en $BUNDLES — este script empaqueta SOLO ORBIT"
  NAME="$(basename "$v" .vst3)"
done
[ -n "$NAME" ] || fail "no encontre ningun .vst3 en $BUNDLES"
[ -d "$BUNDLES/$NAME.component" ] || fail "$NAME.vst3 sin su $NAME.component (el .pkg de macOS lleva los dos)"
log "bundle: $NAME  ·  version $VERSION"

# --- DEFENSA 1: guardia de version. ---
for b in "$BUNDLES/$NAME.vst3" "$BUNDLES/$NAME.component"; do
  bv="$(bundle_version "$b")"
  [ -n "$bv" ] || fail "$(basename "$b") no declara CFBundleShortVersionString"
  [ "$bv" = "$VERSION" ] || fail \
    "$(basename "$b") declara $bv pero se esta empaquetando como $VERSION — reconstruilo con el VERSION bumpeado (o pasa --version $bv)"
done
log "guardia de version: los bundles declaran $VERSION ✓"

# --- GUARDIA DE ARQUITECTURA (pre-vuelo). ---
# SUPERNOVA 0.3.0 estuvo a un pelo de salir arm64-only: el build/ habia quedado en la arquitectura
# nativa y el empaquetador arma el .pkg igual de contento con un payload thin -- el problema recien
# aparece en una Mac Intel, DESPUES de publicar. Este repo tiene varias carpetas de build y solo una
# esta configurada como universal, asi que la trampa esta a un `--bundles` de distancia.
# Se falla ACA, antes de gastar un productbuild, y se vuelve a verificar sobre el payload real en el
# post-check (que es lo unico que se distribuye).
# -L sigue los symlinks: un ejecutable que sea un enlace no es "-type f" y se saltearia en silencio.
check_universal() { # $1 = bundle
  local bin archs n=0
  while IFS= read -r bin; do
    archs="$(lipo -archs "$bin" 2>/dev/null)"
    case " $archs " in *" x86_64 "*) ;; *) fail "no universal: ${bin#"$BUNDLES/"} = ${archs:-<no es Mach-O>}" ;; esac
    case " $archs " in *" arm64 "*)  ;; *) fail "no universal: ${bin#"$BUNDLES/"} = ${archs:-<no es Mach-O>}" ;; esac
    n=$((n+1))
  done < <(find -L "$1/Contents/MacOS" -type f -perm -u+x 2>/dev/null)
  [ "$n" -gt 0 ] || fail "$(basename "$1") no trae ningun ejecutable en Contents/MacOS"
  # OJO: acumula en una global y NO devuelve por stdout. Llamarla dentro de $( ) haria que el
  # `fail` matara solo al subshell y el empaquetado siguiera adelante con un payload thin.
  UNI_BINS=$((UNI_BINS + n))
}
UNI_BINS=0
for b in "$BUNDLES/$NAME.vst3" "$BUNDLES/$NAME.component"; do
  check_universal "$b"
done
log "guardia de arquitectura: $UNI_BINS binarios, todos x86_64 + arm64 ✓"

# --- Payload de cumplimiento AGPLv3: LICENSE + NOTICE + SOURCE. ---
# SOURCE.txt se GENERA (nunca a mano) desde VERSION y el commit del arbol, y apunta al arbol EXACTO
# de esta version: eso es lo que pide el §6 — la fuente CORRESPONDIENTE al binario, no "el repo".
LIC_ROOT="$WORK/root-license/Library/Audio/Plug-Ins/OVNI Audio"
mkdir -p "$LIC_ROOT"
cp "$LICENSE_FILE" "$LIC_ROOT/LICENSE.txt"
cp "$NOTICE_FILE"  "$LIC_ROOT/NOTICE.txt"
COMMIT="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo "desconocido")"
DIRTY=""
[ -n "$(git -C "$ROOT" status --porcelain 2>/dev/null)" ] && DIRTY="  (arbol con cambios sin commitear al empaquetar)"
cat > "$LIC_ROOT/SOURCE.txt" <<SOURCE
============================================================
  OVNI ORBIT — Código fuente / Source code (AGPLv3)
============================================================

ORBIT es software libre bajo AGPLv3 (texto completo en
LICENSE.txt). Tenés derecho al código fuente completo y
correspondiente de ESTA versión / You are entitled to the
complete corresponding source of THIS version:

    https://github.com/ovniaudio/orbita/tree/v$VERSION

Versión / version: $VERSION
Commit:            $COMMIT$DIRTY

(source available per AGPLv3 §6)

El resto del catálogo OVNI / the rest of the OVNI catalog:
    https://github.com/ovniaudio/ovni

¿Dudas? https://ovniaudio.com  ·  hello@ovniaudio.com
============================================================
SOURCE
xattr -cr "$WORK/root-license" 2>/dev/null || true
pkgbuild --root "$WORK/root-license" --identifier "$PKG_ID.license" \
  --version "$VERSION" --install-location "/" "$WORK/pkg-license.pkg" >&2 \
  || fail "pkgbuild license fallo"

# --- Scripts del instalador: el preinstall que retira el ORBIT viejo (D-32). ---
# Va colgado del componente VST3, que es el PRIMERO del choices-outline, y no de los dos: el script
# ya se ocupa del VST3 y del AU en una sola pasada, y colgarlo dos veces solo dejaria dos carpetas
# con fecha en el cajon de retirados. Es idempotente igual.
SCRIPTS="$WORK/scripts"
mkdir -p "$SCRIPTS"
cp "$PREINSTALL" "$SCRIPTS/preinstall"
chmod 755 "$SCRIPTS/preinstall"
# Se limpian los xattr heredados. OJO: com.apple.provenance lo pone el sistema en CUALQUIER archivo
# nuevo de macOS 15+ y `xattr -c` no lo saca, asi que pkgbuild igual mete un AppleDouble de 163 B
# ("._preinstall") al lado del script dentro del paquete. Es inerte -- Installer.app ejecuta
# "preinstall" por nombre -- y no hay forma de evitarlo desde aca; queda anotado para que nadie
# vuelva a perseguirlo.
xattr -cr "$SCRIPTS" 2>/dev/null || true
find "$SCRIPTS" -name '._*' -delete 2>/dev/null

# --- Component packages (VST3 + AU), NO relocalizables, sin chequeo de version. ---
RV="$WORK/root-vst3/Library/Audio/Plug-Ins/VST3"
RA="$WORK/root-au/Library/Audio/Plug-Ins/Components"
mkdir -p "$RV" "$RA"
ditto "$BUNDLES/$NAME.vst3"      "$RV/$NAME.vst3"      || fail "ditto $NAME.vst3"
ditto "$BUNDLES/$NAME.component" "$RA/$NAME.component" || fail "ditto $NAME.component"
find "$WORK/root-vst3" "$WORK/root-au" -name .DS_Store -delete 2>/dev/null
xattr -cr "$WORK/root-vst3" "$WORK/root-au" 2>/dev/null || true

for kind in vst3 au; do
  root="$WORK/root-$kind"
  plist="$WORK/$SLUG-$kind.plist"
  pkgbuild --analyze --root "$root" "$plist" >/dev/null 2>&1 || fail "pkgbuild --analyze $kind"
  i=0
  while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
    plist_set "$plist" "$i:BundleIsRelocatable"    bool   false
    plist_set "$plist" "$i:BundleIsVersionChecked" bool   false
    plist_set "$plist" "$i:BundleOverwriteAction"  string upgrade
    i=$((i+1))
  done
  # bash 3.2 (el de macOS) + `set -u`: expandir un array VACIO con "${a[@]}" es "unbound variable".
  # Por eso el --scripts va en dos ramas y no en un array opcional.
  if [ "$kind" = "vst3" ]; then
    pkgbuild --root "$root" --component-plist "$plist" --scripts "$SCRIPTS" \
      --identifier "$PKG_ID.$SLUG.$kind" --version "$VERSION" \
      --install-location "/" "$WORK/pkg-$SLUG-$kind.pkg" >&2 \
      || fail "pkgbuild $kind fallo"
  else
    pkgbuild --root "$root" --component-plist "$plist" \
      --identifier "$PKG_ID.$SLUG.$kind" --version "$VERSION" \
      --install-location "/" "$WORK/pkg-$SLUG-$kind.pkg" >&2 \
      || fail "pkgbuild $kind fallo"
  fi
done
log "componentes: $PKG_ID.$SLUG.vst3 (con preinstall) · $PKG_ID.$SLUG.au · $PKG_ID.license"

# --- Branding + textos del instalador (EN + ES). Sin arte, sale igual, sin marca. ---
RES="$WORK/res"
mkdir -p "$RES/en.lproj" "$RES/es.lproj"
cp "$LICENSE_FILE" "$RES/LICENSE.txt"
BG_XML=""
if [ -f "$ART_DIR/bg-orbit-light.png" ] && [ -f "$ART_DIR/bg-orbit-dark.png" ]; then
  cp "$ART_DIR/bg-orbit-light.png" "$RES/background.png"
  cp "$ART_DIR/bg-orbit-dark.png"  "$RES/background-dark.png"
  BG_XML=$(printf '    <background file="background.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>\n    <background-darkAqua file="background-dark.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>')
else
  log "aviso: sin arte en $ART_DIR — el instalador sale sin branding"
fi

UPGRADE_EN='<p>Upgrading from 0.2.x? The bundles are now named <b>OVNI ORBIT</b>. The installer retires your old <code>ORBIT.vst3</code>/<code>ORBIT.component</code> (only if it is ours), moving it to <code>Library/Application Support/OVNI Audio/replaced/</code> — another vendor&#39;s <i>Orbit</i> is never touched.</p>'
UPGRADE_ES='<p>¿Venís de 0.2.x? Los bundles ahora se llaman <b>OVNI ORBIT</b>. El instalador retira tu <code>ORBIT.vst3</code>/<code>ORBIT.component</code> viejo (solo si es el nuestro) y lo guarda en <code>Library/Application Support/OVNI Audio/replaced/</code> — el <i>Orbit</i> de otra marca no se toca.</p>'
cat > "$RES/en.lproj/welcome.html" <<HTML
<!doctype html><html><head><meta charset="utf-8"><style>body{font-family:-apple-system,'Helvetica Neue',sans-serif;font-size:13px}</style></head><body>
<p><b>ORBIT — Binaural movement engine: place a sound around your head, orbit it, or fly it past with real Doppler.</b></p>
<p>Free &amp; open-source (AGPLv3), by OVNI Audio. The installer places everything in the system plug-in folders — no dragging, no Terminal:</p>
<p>&nbsp;&nbsp;VST3 → /Library/Audio/Plug-Ins/VST3<br>&nbsp;&nbsp;AU → /Library/Audio/Plug-Ins/Components</p>
$UPGRADE_EN
<p>Manuals &amp; the rest of the catalog: <b>ovniaudio.com</b></p>
</body></html>
HTML
cat > "$RES/es.lproj/welcome.html" <<HTML
<!doctype html><html><head><meta charset="utf-8"><style>body{font-family:-apple-system,'Helvetica Neue',sans-serif;font-size:13px}</style></head><body>
<p><b>ORBIT — Motor de movimiento binaural: poné un sonido alrededor de tu cabeza, hacelo orbitar o pasalo cerca con Doppler real.</b></p>
<p>Gratis y open-source (AGPLv3), de OVNI Audio. El instalador deja todo en las carpetas de plugins del sistema — sin arrastrar nada, sin Terminal:</p>
<p>&nbsp;&nbsp;VST3 → /Library/Audio/Plug-Ins/VST3<br>&nbsp;&nbsp;AU → /Library/Audio/Plug-Ins/Components</p>
$UPGRADE_ES
<p>Manuales y el resto del catálogo: <b>ovniaudio.com</b></p>
</body></html>
HTML
cat > "$RES/en.lproj/conclusion.html" <<'HTML'
<!doctype html><html><head><meta charset="utf-8"><style>body{font-family:-apple-system,'Helvetica Neue',sans-serif;font-size:13px}</style></head><body>
<p><b>Done.</b> Open your DAW and rescan plug-ins — ORBIT will show up in your list (VST3 and AU).</p>
<p>Coming from 0.2.x? macOS caches the Audio Unit registry by path: run <code>killall -9 AudioComponentRegistrar</code> in Terminal before rescanning.</p>
<p>Manuals &amp; support: <b>ovniaudio.com</b> · hello@ovniaudio.com &nbsp;🛸</p>
</body></html>
HTML
cat > "$RES/es.lproj/conclusion.html" <<'HTML'
<!doctype html><html><head><meta charset="utf-8"><style>body{font-family:-apple-system,'Helvetica Neue',sans-serif;font-size:13px}</style></head><body>
<p><b>Listo.</b> Abrí tu DAW y reescaneá los plugins — ORBIT va a aparecer en tu lista (VST3 y AU).</p>
<p>¿Venís de 0.2.x? macOS cachea el registro de Audio Units por ruta: corré <code>killall -9 AudioComponentRegistrar</code> en la Terminal antes de reescanear.</p>
<p>Manuales y soporte: <b>ovniaudio.com</b> · hello@ovniaudio.com &nbsp;🛸</p>
</body></html>
HTML

# --- DEFENSA 3: post-check sobre el .pkg emitido. ---
verify_pkg() { # $1 = .pkg
  local pkg="$1" n x pi comp v seen=0
  n="$(basename "$pkg")"
  x="$WORK/verify-${n%.pkg}"
  rm -rf "$x"
  pkgutil --expand-full "$pkg" "$x" >/dev/null 2>&1 || fail "post-check: pkgutil --expand-full fallo en $n"
  [ -f "$x/Distribution" ] || fail "post-check: $n no tiene Distribution"

  for pi in "$x"/*.pkg/PackageInfo; do
    [ -f "$pi" ] || fail "post-check: $n no trae ningun component package adentro"
    comp="$(basename "$(dirname "$pi")")"
    v="$(xml_attr pkg-info version < "$pi" | head -1)"
    [ "$v" = "$VERSION" ] || fail "post-check: $n → $comp/PackageInfo declara version=${v:-<vacio>} (esperaba $VERSION)"
    seen=$((seen+1))
  done
  [ "$seen" -gt 0 ] || fail "post-check: $n sin componentes verificables"

  while read -r v; do
    [ "$v" = "$VERSION" ] || fail "post-check: $n → Distribution tiene un <pkg-ref version=\"$v\"> (esperaba $VERSION)"
  done < <(xml_attr pkg-ref version < "$x/Distribution")
  for attr in CFBundleShortVersionString CFBundleVersion; do
    while read -r v; do
      [ "$v" = "$VERSION" ] || fail "post-check: $n → Distribution tiene un <bundle $attr=\"$v\"> (esperaba $VERSION)"
    done < <(xml_attr bundle "$attr" < "$x/Distribution")
  done

  # El preinstall tiene que estar DENTRO del paquete, no solo en el repo.
  [ -f "$x/pkg-$SLUG-vst3.pkg/Scripts/preinstall" ] \
    || fail "post-check: $n no lleva el preinstall adentro (pkg-$SLUG-vst3.pkg/Scripts/preinstall)"

  # GUARDIA DE ARQUITECTURA sobre el payload REAL (--expand-full, no --expand): se mira el binario
  # que de verdad va adentro del paquete, no el que habia en disco cuando se lanzo el script.
  local bin archs bins=0 bundle
  while IFS= read -r bin; do
    archs="$(lipo -archs "$bin" 2>/dev/null)"
    case " $archs " in *" x86_64 "*) ;; *) fail "post-check: no universal en el payload: ${bin#"$x/"} = ${archs:-<no es Mach-O>}" ;; esac
    case " $archs " in *" arm64 "*)  ;; *) fail "post-check: no universal en el payload: ${bin#"$x/"} = ${archs:-<no es Mach-O>}" ;; esac
    bins=$((bins+1))
  done < <(find -L "$x" -type f -path "*/Contents/MacOS/*" -perm -u+x)
  [ "$bins" -gt 0 ] || fail "post-check: $n no trae ningun ejecutable en el payload (¿bundles vacios?)"
  # …y POR BUNDLE, no en total: contar todo junto deja pasar un .component vacio colgado del .vst3.
  while IFS= read -r bundle; do
    [ -n "$(find -L "$bundle/Contents/MacOS" -type f -perm -u+x -print -quit 2>/dev/null)" ] \
      || fail "post-check: $n → ${bundle#"$x/"} sin ejecutable en Contents/MacOS"
  done < <(find "$x" \( -name "*.vst3" -o -name "*.component" \) -type d)

  rm -rf "$x"
  log "  post-check OK: $n declara $VERSION en $seen componentes + Distribution · preinstall adentro · $bins binarios universales"
}

# --- Distribution + productbuild. ---
cat > "$WORK/dist.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>OVNI Audio — $TITLE_NAME (v$VERSION)</title>
    <organization>com.ovni</organization>
${BG_XML}
    <welcome file="welcome.html"/>
    <license file="LICENSE.txt"/>
    <conclusion file="conclusion.html"/>
    <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/><line choice="au"/><line choice="license"/>
    </choices-outline>
    <choice id="vst3" title="$TITLE_NAME VST3"><pkg-ref id="$PKG_ID.$SLUG.vst3"/></choice>
    <choice id="au" title="$TITLE_NAME AU"><pkg-ref id="$PKG_ID.$SLUG.au"/></choice>
    <choice id="license" title="License &amp; source (AGPLv3)" enabled="false" selected="true">
        <pkg-ref id="$PKG_ID.license"/>
    </choice>
    <pkg-ref id="$PKG_ID.$SLUG.vst3" version="$VERSION">pkg-$SLUG-vst3.pkg</pkg-ref>
    <pkg-ref id="$PKG_ID.$SLUG.au" version="$VERSION">pkg-$SLUG-au.pkg</pkg-ref>
    <pkg-ref id="$PKG_ID.license" version="$VERSION">pkg-license.pkg</pkg-ref>
</installer-gui-script>
XML

# Se emite a un temporal y recien se mueve al outdir DESPUES del post-check: un .pkg rechazado no
# tiene que quedar en la carpeta de entrega, donde alguien lo pueda firmar o subir por error.
OUT="$OUTDIR/OVNI-ORBIT-v$VERSION.pkg"
TMP_OUT="$WORK/OVNI-ORBIT-v$VERSION.pkg"
if [ -n "$INSTALLER_SIGN_ID" ]; then
  productbuild --distribution "$WORK/dist.xml" --package-path "$WORK" --resources "$RES" \
    --version "$VERSION" --sign "$INSTALLER_SIGN_ID" "$TMP_OUT" >&2 || fail "productbuild (firmado)"
else
  productbuild --distribution "$WORK/dist.xml" --package-path "$WORK" --resources "$RES" \
    --version "$VERSION" "$TMP_OUT" >&2 || fail "productbuild"
fi
verify_pkg "$TMP_OUT"
mv "$TMP_OUT" "$OUT" || fail "no pude mover el .pkg verificado a $OUT"
log "✓ $OUT"

( cd "$OUTDIR" && shasum -a 256 "OVNI-ORBIT-v$VERSION.pkg" > SHA256SUMS.txt )
log "✓ $OUTDIR/SHA256SUMS.txt"
[ -z "$INSTALLER_SIGN_ID" ] && log "instalador SIN FIRMAR (camino gratis): Gatekeeper avisa al abrir el .pkg → click derecho → Abrir (macOS ≤14) / Ajustes → Privacidad y seguridad → Abrir de todos modos (15+)."
exit 0
