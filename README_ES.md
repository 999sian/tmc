<p align="center">
  <img src="docs/images/portada-aleks-version.png"
       alt="The Legend of Zelda: The Minish Cap – Alek's Ultimate NX Edition"
       width="460">
</p>

# The Legend of Zelda: The Minish Cap — Alek's Ultimate NX Edition

**v0.1.1** · Port homebrew nativo para Nintendo Switch · [English → README.md](README.md)

![Juego a doble pantalla en Nintendo Switch](docs/screenshots/hero/01_hero_dual.jpg)

## ¿Qué es esto?

Una edición personal, centrada en Nintendo Switch, del port nativo de *The
Minish Cap*, construida sobre la descompilación de código abierto y los
proyectos de port nativo listados en [CREDITS.md](CREDITS.md). El juego corre
de forma nativa en la Switch — no es un emulador — y añade una segunda
pantalla de compañía, interacción táctil, RetroAchievements, una interfaz del
port localizada y varias mejoras de calidad de vida específicas de Switch.

Lo hice principalmente porque quería jugar *The Minish Cap* de esta manera
concreta en mi propia Switch: con una disposición a doble pantalla al estilo
de los Zelda de DS, un modo vertical tipo Flip Grip y logros. Se comparte
públicamente por si otras personas quieren jugarlo, estudiarlo, hacer un fork
o usarlo como punto de partida para su propio trabajo.

Este proyecto **no** creó la descompilación de Minish Cap, ni el port nativo
para PC, ni el concepto original de doble pantalla. Se apoya en los proyectos
acreditados — consulta [CREDITS.md](CREDITS.md) para el linaje completo.

## Características

- **Compilación nativa para Switch** — sin emulador, funciona como homebrew (NRO).
- **Tres modos de pantalla**
  - **NORMAL** — presentación clásica a una sola pantalla.
  - **DUAL** — el juego más un panel de segunda pantalla, lado a lado.
  - **FLIP 270** — composición vertical pensada para girar físicamente la
    Switch (funciona muy bien con un soporte tipo Flip Grip; sin afiliación
    con ningún fabricante de accesorios).
- **Panel de segunda pantalla** con cuatro pestañas:
  - **MISIÓN** — misión principal actual, objetivo, pista y ubicación (Guía de
    Historia).
  - **MAPA** — mapas del mundo y de mazmorras con marcador del jugador en vivo
    y cámara de seguimiento opcional.
  - **OBJETOS** — vista de equipo con los anillos A/B y los atajos X/Y/ZL/ZR.
  - **CONFIG** — todos los ajustes del port.
- Interacción **táctil** en el panel (pestañas, ajustes, asignación de objetos).
- **RetroAchievements** — inicio de sesión, reconocimiento del juego, Rich
  Presence, notificaciones reales de desbloqueo con la insignia real, y cierre
  de sesión. Requiere una cuenta gratuita de
  [retroachievements.org](https://retroachievements.org).
- **Autoguardado gestionado por el port** y **Cargar Autoguardado** —
  separados del sistema de guardado propio del juego; nunca lo sobrescriben.
- **Volver al Título** desde el menú.
- **Interfaz del port localizada** en English, Español, Français, Deutsch e
  Italiano. (La localización de los diálogos originales del juego es de
  Nintendo; este proyecto solo localiza la interfaz añadida por el port.)
- **Ayuda de Acción** — la ayuda contextual del botón R en el panel puede
  ponerse en NO / CONTEXTUAL / SÍ.
- **Tiempo de arranque sustancialmente reducido** (Fast Boot). Se observó un
  tiempo de inicio de aproximadamente 13–14 segundos en el hardware probado
  por el mantenedor; el tiempo real puede variar según la tarjeta SD y las
  condiciones del sistema.

El widescreen real **no** forma parte de la v0.1.1.

## Capturas

| DUAL | NORMAL | FLIP 270 |
|---|---|---|
| ![DUAL](docs/screenshots/gameplay/02_dual_gameplay.jpg) | ![NORMAL](docs/screenshots/gameplay/03_normal.jpg) | ![FLIP](docs/screenshots/flip/06_flip270.jpg) |

Desbloqueo real de un logro, en hardware real, en el Santuario del Bosque:

![Desbloqueo de RetroAchievements](docs/screenshots/achievements/08_retroachievements_dungeon_map.jpg)

### Localización de la interfaz del port

<table>
  <tr>
    <td align="center"><b>English</b></td>
    <td align="center"><b>Español</b></td>
    <td align="center"><b>Français</b></td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/localization/en.jpg" alt="Interfaz del port en inglés" width="280"></td>
    <td><img src="docs/screenshots/localization/es.jpg" alt="Interfaz del port en español" width="280"></td>
    <td><img src="docs/screenshots/localization/fr.jpg" alt="Interfaz del port en francés" width="280"></td>
  </tr>
  <tr>
    <td align="center"><b>Deutsch</b></td>
    <td align="center"><b>Italiano</b></td>
    <td></td>
  </tr>
  <tr>
    <td><img src="docs/screenshots/localization/de.jpg" alt="Interfaz del port en alemán" width="280"></td>
    <td><img src="docs/screenshots/localization/it.jpg" alt="Interfaz del port en italiano" width="280"></td>
    <td></td>
  </tr>
</table>

La captura en inglés muestra la pestaña MAPA y las otras cuatro muestran
MISIÓN — en las cinco se ve la interfaz propia del port en ese idioma. Más en
[docs/SCREENSHOTS.md](docs/SCREENSHOTS.md).

### Localización y ROM base

Alek's Ultimate NX Edition está probado principalmente con la **ROM USA** como
base recomendada. El port añade su propia capa de interfaz multilenguaje para
los menús y funciones específicas del port en inglés, español, francés, alemán
e italiano.

El contenido y las traducciones originales del juego siguen perteneciendo a los
datos originales del juego; este proyecto no se atribuye las traducciones
oficiales de Nintendo. Poner la interfaz del port en español no convierte una
ROM USA en otra edición regional — el idioma de los diálogos del juego es el
que proporcione tu propia ROM.

## Instalación

Versión corta — guía completa en
[docs/INSTALLATION_ES.md](docs/INSTALLATION_ES.md):

1. Copia el NRO en `/switch/tmc/` de tu tarjeta SD.
2. Copia tu ROM **propia y obtenida legalmente** de *The Minish Cap* versión
   **USA** (`.gba`) en la misma carpeta. El nombre del archivo no importa — el
   port identifica la ROM por su cabecera. `baserom.gba` es el nombre
   convencional.
3. Lánzalo desde el Homebrew Menu. Se recomienda el modo aplicación (lanzar
   sobre un título completo) para el mejor rendimiento.

**Este proyecto no incluye ninguna ROM, no proporciona enlaces de descarga de
ROMs y no distribuye contenido del juego con derechos de autor.** Debes volcar
tu propio cartucho u obtener el juego legalmente.

## Compatibilidad de ROM

La versión v0.1.1 está construida para la edición **USA** (cabecera `BZME`).
El volcado USA de referencia conocido tiene el SHA-1
`b4bd50e4131b027c334547b4524e2dbbd4227130` — el port no verifica este hash en
tiempo de ejecución (identifica el juego por la cabecera), pero ese es el
volcado con el que esta versión se compiló y probó. El cargador reconoce
cabeceras EU (`BZMP`), pero la v0.1.1 se compila y prueba como edición USA;
usa una ROM USA.

## Rendimiento

NORMAL funciona a 60 FPS con el mayor margen. DUAL y FLIP son más exigentes
que NORMAL. En el hardware probado, se recomienda una frecuencia de CPU de
1224 MHz para acercarse más a una experiencia estable de 60 FPS. Es opcional y
no garantiza 60 FPS fijos en todas las escenas. El port nunca cambia la
frecuencia por su cuenta; la integración automática con gestores de reloj sigue
en evaluación. Detalles en [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Configuración

Todos los ajustes de CONFIG (JUEGO, CONTROLES, PANTALLA, LOGROS, SISTEMA)
están documentados en [docs/CONFIGURATION_ES.md](docs/CONFIGURATION_ES.md).

## Transparencia de desarrollo

Este proyecto se desarrolló con una asistencia significativa de IA — incluidas
Claude / Claude Code, ChatGPT y OpenAI Codex — para investigación de código,
depuración, implementación, revisión de código, documentación y preparación de
la publicación. La dirección del proyecto, las decisiones de características,
las pruebas en hardware real, la validación visual y todas las decisiones de
publicación las tomó manualmente el mantenedor. Esto describe cómo se
construyó *esta edición*; los proyectos upstream sobre los que se apoya tienen
sus propias historias de desarrollo, y aquí no se hace ninguna suposición
sobre las herramientas o los flujos de trabajo que hayan usado sus
mantenedores. La descripción completa está en
[docs/DEVELOPMENT_TRANSPARENCY.md](docs/DEVELOPMENT_TRANSPARENCY.md).

## Expectativas de mantenimiento

Este es un proyecto personal. Mi intención es seguir mejorándolo hasta que
alcance un estado que considere totalmente jugable y estable para el uso que
le quiero dar. Después de eso, las actualizaciones pueden volverse poco
frecuentes o detenerse. No prometo soporte a largo plazo, ni una hoja de ruta,
ni respuestas rápidas a los issues. Si quieres llevarlo más lejos, los forks
son bienvenidos bajo las licencias upstream aplicables — consulta
[CONTRIBUTING.md](CONTRIBUTING.md).

## Problemas conocidos

Consulta [docs/KNOWN_ISSUES_ES.md](docs/KNOWN_ISSUES_ES.md). Dos notas
destacadas: en escenas exigentes DUAL y FLIP pueden bajar de 60 FPS, y tras
visitar el barril giratorio de Deepwood Shrine, volver a la pantalla de título
puede provocar artefactos visuales/afines temporales — reiniciar la aplicación
restaura la pantalla de título normalmente.

## Créditos y linaje

Esta edición existe gracias a los proyectos siguientes. La versión corta:

- [zeldaret/tmc](https://github.com/zeldaret/tmc) — la descompilación de
  Minish Cap sobre la que se construye todo.
- [Project Picori (999sian/tmc)](https://github.com/999sian/tmc) — la base del
  port nativo para PC.
- [samyost1/tmc-android](https://github.com/samyost1/tmc-android) — el
  concepto e implementación de doble pantalla que este trabajo extiende.
- [EstebanPdN/zelda-tmc-3ds](https://github.com/EstebanPdN/zelda-tmc-3ds) —
  la adaptación a doble pantalla para 3DS usada como referencia.
- [HayatoG/tmc](https://github.com/hayatog/tmc) — la base directa de la que
  parte este árbol para Switch.

Roles completos, bibliotecas de terceros y detalles de licencias:
[CREDITS.md](CREDITS.md) · [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

Este es un proyecto de fans no oficial, sin afiliación, respaldo ni soporte de
Nintendo. *The Legend of Zelda* y *The Minish Cap* son marcas de Nintendo. Con
este proyecto no se distribuyen recursos originales del juego, ROMs ni ningún
contenido de Nintendo con derechos de autor.
