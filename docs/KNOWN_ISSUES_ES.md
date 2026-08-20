# Problemas conocidos — v0.1.0

*[English version → KNOWN_ISSUES.md](KNOWN_ISSUES.md)*

Limitaciones actuales, indicadas con honestidad. Los problemas ya corregidos no
aparecen en esta lista.

## Rendimiento

- **DUAL y FLIP pueden bajar de 60 FPS en escenas exigentes** (zonas con mucha
  actividad como el Bosque Minish son el peor caso). NORMAL tiene el mayor
  margen y se mantiene a 60 durante el juego normal. Consulta
  [PERFORMANCE.md](PERFORMANCE.md) para la recomendación opcional de 1224 MHz.

## Pantalla / renderizado

- **Pantalla de título tras el barril de Deepwood Shrine (severidad baja).**
  Después de visitar el barril giratorio de Deepwood Shrine, volver a la
  pantalla de título puede provocar artefactos visuales/afines temporales.
  Reiniciar la aplicación restaura la pantalla de título normalmente. La
  pantalla de título funciona con normalidad en un arranque limpio, y la
  propia sala del barril se renderiza correctamente — esto afecta únicamente
  al regreso al título dentro de la misma sesión.

## Pantalla / mapa

- En algunos interiores el mapa de la segunda pantalla oculta a propósito el
  marcador del jugador cuando la sala no tiene una posición válida en el mundo
  exterior. Es intencionado, no un marcador perdido.

## Funciones que no están en la v0.1.0

- El **widescreen real** no está disponible en esta versión (investigado y
  previsto para una versión posterior).
- El **guardado manual en cualquier lugar** no está expuesto; usa el guardado
  normal del juego o el AUTOGUARDADO / CARGAR AUTO del port.
- No hay soporte de RetroAchievements sin conexión; RA necesita conexión de
  red durante el juego.
- No hay actualizador integrado; las actualizaciones son sustituyendo el NRO
  manualmente.

## Entorno

- El modo applet (lanzar sin mantener un título completo) no está probado y
  puede tener problemas de memoria; **se recomienda el modo aplicación**.
- En modo dock se renderiza a la misma resolución interna (la consola la
  escala); las disposiciones están pensadas para la pantalla portátil de
  1280×720.

Si encuentras algo que no está en esta lista, abre un issue indicando tu
hardware y los pasos para reproducirlo (consulta
[../CONTRIBUTING.md](../CONTRIBUTING.md)).
