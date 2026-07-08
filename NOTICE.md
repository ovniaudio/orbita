# Avisos de terceros — ORBIT 🛸

**ORBIT** es el FX de movimiento espacial binaural del sello **OVNI**. Se distribuye bajo
**AGPLv3** (ver [`LICENSE`](LICENSE)), gratis y open-source, como el resto del catálogo.
Este archivo lista todo el software y los datos de terceros que ORBIT usa o incorpora. La
marca OVNI se sostiene en **honestidad verificable**: si algo de acá no se puede comprobar,
es un bug — abrí un issue.

> **Nota:** ORBIT viaja también en el DMG del catálogo (`OVNI-v0.1.0.dmg`, del repo
> [ovniaudio/ovni](https://github.com/ovniaudio/ovni)) junto a los 6 plugins del catálogo.
> La atribución de **Intel IPP** aparece por eso tanto acá como en el
> [`NOTICE.md` del catálogo](https://github.com/ovniaudio/ovni/blob/main/NOTICE.md). Los 6
> plugins OVNI **no** usan IPP; **ORBIT sí**.

---

## Decisión de licencia

**JUCE 8.0.13** (la versión pineada en el submódulo `JUCE/`) en su edición gratuita es
**AGPLv3** (GNU Affero GPL v3) o licencia comercial (dual). ORBIT sale bajo **AGPLv3**:
coincide con los términos open-source de JUCE, y publicamos el código completo del repo en
el release, lo que cumple las obligaciones del AGPL (incluida la cláusula de red de la
sección 13, que de todos modos no aplica a un plugin de escritorio que no se sirve por red).
El `LICENSE` de la raíz ya es el texto oficial completo de la AGPLv3.

> Si en el futuro ORBIT se vendiera cerrado, aplicaría el tier gratuito de **JUCE Starter**
> (hasta US$20k/año de facturación; pasado ese umbral, JUCE Indie, US$800 pago único). Eso
> se documenta sólo a nivel decisión y no cambia mientras ORBIT se distribuya gratis bajo
> AGPLv3.

---

## Software

### JUCE
- **Origen / autor:** Raw Material Software Ltd. — <https://juce.com>
- **Versión:** 8.0.13 (submódulo `JUCE/`)
- **Licencia:** edición gratuita **AGPLv3** + licencia comercial (dual). ORBIT sale bajo
  AGPLv3 (ver "Decisión de licencia" arriba).
- **Uso en el proyecto:** framework de audio/UI del plugin (`juce_audio_processors`,
  `juce_dsp`, `juce_gui_basics`, etc.).

### Intel Integrated Performance Primitives (IPP)
- **Origen / autor:** Intel Corporation — <https://www.intel.com/content/www/us/en/developer/tools/oneapi/ipp.html>
- **Versión:** IPP 2021.9.x (librerías estáticas redistribuibles de Intel para macOS
  x86_64; configurado en `cmake/PamplejuceIPP.cmake`).
- **Licencia / términos:** IPP se redistribuye bajo la licencia propietaria de Intel (Intel
  Simplified Software License / términos de redistribución de oneAPI), que permite incluir
  las librerías estáticas ya compiladas dentro de un producto binario. No reproducimos acá
  el texto de esa licencia; consultar los términos oficiales de Intel para el detalle.
- **Uso en el proyecto:** **dependencia binaria de ORBIT** para acelerar DSP. IPP es
  **x86_64 únicamente**: en el binario universal, las rutinas de IPP se compilan y enlazan
  **estáticamente sólo en la porción Intel (x86_64)** vía `-Xarch_x86_64` + la macro
  `PAMPLEJUCE_IPP`; la porción **Apple Silicon (arm64) usa JUCE/sistema puro, sin IPP**. Al
  enlazarse estáticamente, el usuario final **no** necesita tener IPP instalado.

### melatonin_inspector
- **Origen / autor:** Sudara Williams — <https://github.com/sudara/melatonin_inspector>
- **Licencia:** **MIT**
- **Uso en el proyecto:** módulo JUCE de inspección de UI para desarrollo (`modules/melatonin_inspector`).

### clap-juce-extensions
- **Origen / autor:** Paul Walker y colaboradores — <https://github.com/free-audio/clap-juce-extensions>
- **Licencia:** **MIT**
- **Uso en el proyecto:** módulo presente en el árbol (`modules/clap-juce-extensions`), pero
  el formato **CLAP está DESHABILITADO** en ORBIT: el release ship sólo **AU + VST3**.

### libmysofa
- **Origen / autor:** Christian Hoene y colaboradores — <https://github.com/hoene/libmysofa>
- **Licencia:** **BSD-3-Clause**
- **Uso en el proyecto:** carga de SOFA/AES69 **sólo al compilar tests** (para generar el
  HRIR offline con `tests/GenHrir.cpp`). **No** se enlaza en el binario de release de ORBIT.

### Catch2
- **Origen / autor:** Catch2 contributors (catchorg) — <https://github.com/catchorg/Catch2>
- **Versión:** v3.8.1 (CPM, sólo al compilar tests)
- **Licencia:** **BSL-1.0** (Boost Software License 1.0)
- **Uso en el proyecto:** framework de tests. **No** se enlaza en los binarios de release.

> ORBIT parte del template **Pamplejuce** (Sudara, MIT). Los scaffolds de CMake/CI heredados
> de Pamplejuce quedan bajo su licencia MIT original.

---

## Fuentes tipográficas (bundleadas)

Estas tipografías se incluyen en `assets/fonts/` y se embeben en el binario para la UI:

### Clash Grotesk · General Sans
- **Origen / autor:** Indian Type Foundry — <https://www.fontshare.com>
- **Licencia:** **Fontshare / ITF Free Font License** (uso gratuito, personal y comercial;
  redistribución permitida como parte de un producto).
- **Uso en el proyecto:** tipografías de la interfaz de ORBIT (`ClashGrotesk-Semibold`,
  `GeneralSans-Medium`, `GeneralSans-Regular`).

### JetBrains Mono
- **Origen / autor:** JetBrains — <https://www.jetbrains.com/lp/mono/>
- **Licencia:** **SIL Open Font License 1.1** (OFL-1.1).
- **Uso en el proyecto:** tipografía monoespaciada de la UI (`JetBrainsMono-Regular`).

---

## Datasets HRIR / HRTF

### CIPIC HRTF Database — subject 003  ← **HRIR horneada en ORBIT**
- **Origen / autor:** CIPIC Interface Laboratory, U.C. Davis — V. R. Algazi, R. O. Duda,
  D. M. Thompson, C. Avendano. <https://www.ece.ucdavis.edu/cipic/>
- **Licencia / términos:** distribución libre, de **dominio público para investigación**;
  pide atribución a los autores.
- **Cita:** V. R. Algazi, R. O. Duda, D. M. Thompson, C. Avendano, *"The CIPIC HRTF
  Database"*, Proc. 2001 IEEE Workshop on Applications of Signal Processing to Audio
  and Acoustics (WASPAA), pp. 99–102, 2001.
- **Uso en el proyecto:** la HRIR horneada en `source/dsp/HrirData.h` y `source/dsp/HrirRing.h`
  (218 taps, 48 kHz) **deriva de CIPIC subject 003**. El bake se ejecuta offline con
  `tests/GenHrir.cpp` (simetrización L/R + reconstrucción); el SOFA original NO se commitea.
  El sello OVNI migró su HRIR de producción a **SADIE II KU100** (Apache-2.0) por su licencia
  más limpia para uso comercial — ver el [`NOTICE.md` del catálogo](https://github.com/ovniaudio/ovni/blob/main/NOTICE.md);
  ORBIT conserva el bake CIPIC histórico.

---

## Notas de cumplimiento

- **Impulse Responses (IRs):** sólo IRs propias o CC0 / permisivas. Nunca IRs extraídas de
  productos comerciales.
- **Intel IPP** se enlaza estáticamente sólo en la porción x86_64 y se redistribuye conforme
  a los términos de redistribución de Intel; no se modifica ni se reempaqueta su fuente.
- **La HRIR horneada** deriva de **CIPIC subject 003** (dominio público para investigación,
  con atribución a los autores); el SOFA no se commitea (vive fuera del repo).
