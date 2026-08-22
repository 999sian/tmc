# Referencia de configuración (v0.1.2)

Abre el panel de segunda pantalla y selecciona **CONFIG** (en DUAL/FLIP es la
pestaña de la derecha; las etiquetas siguen el IDIOMA elegido). Todos los
ajustes se aplican al momento y se conservan entre reinicios salvo que se
indique lo contrario. Abajo se muestran las etiquetas en español donde la
interfaz las traduce.

## JUEGO (GAMEPLAY)

| Ajuste | Valores | Por defecto | Función |
|---|---|---|---|
| MARCAS DE VIENTO | SÍ / NO | SÍ | Muestra las crestas de viento en el mapa del panel. |
| VOLVER AL PISO | SÍ / NO | SÍ | El mapa de mazmorra vuelve solo a tu piso actual tras consultar otro. |
| MANTENER AVANZA | SÍ / NO | NO | Mantén A para avanzar el texto de los diálogos sin pulsar en cada cuadro. |
| GUÍA DE HISTORIA | NO / PISTA / GUIADA | PISTA | Cuánto cuenta la pestaña MISIÓN. PISTA responde "¿y ahora qué?" sin indicaciones exactas; GUIADA es más explícita; NO la oculta. |
| AYUDA DE ACCIÓN | NO / CONTEXTUAL / SÍ | CONTEXTUAL | La ayuda del botón R en el lateral del panel. NO nunca la muestra; CONTEXTUAL solo cuando hay una acción disponible (comportamiento original); SÍ mantiene visible la placa R durante el juego normal, y la etiqueta de acción solo aparece cuando existe una. Solo afecta a la visibilidad — nunca cambia los controles. |
| PERFIL MEJORADO | CRISP / SMOOTH | CRISP | Sabor del remuestreador de audio. Solo visible con MODO DE AUDIO en ENHANCED. |

## CONTROLES

| Ajuste | Valores | Por defecto | Función |
|---|---|---|---|
| GUÍA DE CONTROLES | — | — | Pantalla de referencia (solo lectura) del esquema completo. |
| GBA A / GBA B / GBA L / GBA R / START / SELECT | cualquier botón/stick | A / B / L / R / PLUS / MINUS | Reasigna los seis controles de GBA. |
| HABLAR CON EZLO | NO / CONTEXT L / X / Y / ZL / ZR / LSTICK / RSTICK | CONTEXT L | Atajo dedicado para hablar con Ezlo (la GBA usaba SELECT; en este port MINUS abre los ajustes). CONTEXT L comparte L con Fusionar de forma contextual. |
| QUICK DISPLAY | PLUS + ZR (fijo) | — | Combinación fija (solo lectura) para cambiar rápido el modo de pantalla. |
| ATAJO X / Y / ZL / ZR | cambia qué objeto ocupa la ranura | sin asignar | Ranuras extra de equipo en los cuatro botones libres — físicamente X, Y, ZL y ZR. Estas filas eligen el **objeto**; el botón es fijo. A/B siguen siendo el equipo principal de GBA. |
| RESTABLECER | — | — | Restaura todas las asignaciones por defecto. |

## PANTALLA (DISPLAY)

| Ajuste | Valores | Por defecto | Función |
|---|---|---|---|
| CÁMARA SIGUE | SÍ / NO | SÍ | El mapa del panel sigue al jugador. |
| FONDO DEL PANEL | pergamino / crema / oscuro | pergamino | Estilo de fondo del panel. |
| MODO PANTALLA | NORMAL / DUAL / FLIP 270 | NORMAL | La presentación general (ver README). |
| ESCALA DE PANTALLA *(solo NORMAL)* | AJUSTE / 2X / 3X / 4X | AJUSTE | Escalado entero a una sola pantalla. |
| ESCALA DEL JUEGO *(solo DUAL)* | 2X / 3X | 2X | Escala del juego en DUAL (2X = 480×320, 3X = 720×480). |
| TAMAÑO PANEL *(solo DUAL)* | 70 / 75 / 80 / 90 % | 90 | Tamaño del panel en DUAL. Las opciones se limitan para que el par siempre quepa con la ESCALA DEL JUEGO actual. |
| SEPARACIÓN *(solo DUAL)* | 0 / 8 / 16 / 24 / 32 / 48 / 64 px | 64 | Espacio entre juego y panel en DUAL. |
| TAMAÑO PANEL *(solo FLIP)* | 70–100 % en pasos de 5 | 75 | Tamaño del panel en FLIP 270. |
| SEPARACIÓN *(solo FLIP)* | 0 / 8 / 16 / 24 / 32 / 48 / 64 / 80 px | 24 | Espacio entre juego y panel en FLIP 270. |

Las filas específicas de un modo solo aparecen mientras ese modo está activo.

## LOGROS (ACHIEVEMENTS)

| Ajuste | Valores | Por defecto | Función |
|---|---|---|---|
| ESTILO DE AVISO | 6 estilos | estilo 2 | Estilo visual de la notificación de desbloqueo. |
| CUENTA | — | sesión cerrada | Inicia sesión en RetroAchievements (teclado en pantalla). Con sesión iniciada muestra el estado de tu cuenta. |
| TEST NOTIFICATION | FIRE | — | Previsualiza el aviso localmente. Nunca contacta con el servidor ni otorga nada. |
| CERRAR SESIÓN | — | — | Solo aparece con sesión iniciada. Elimina únicamente el token RA guardado; partidas, configuración y logros del servidor quedan intactos. |

## SISTEMA

| Ajuste | Valores | Por defecto | Función |
|---|---|---|---|
| IDIOMA | ENGLISH / ESPAÑOL / FRANÇAIS / DEUTSCH / ITALIANO | ENGLISH | Idioma de la interfaz propia del port (panel, ajustes, Guía de Historia). El idioma de los diálogos originales es el de la ROM. |
| AUTOGUARDADO | SÍ / NO | NO | Autoguardado gestionado por el port en su propio archivo (`autosave.bin`). Nunca toca el guardado normal del juego. |
| CARGAR AUTO | LISTO (acción) | — | Restaura el autoguardado más reciente del port. Sin autoguardado previo, no hace nada. |
| MODO DE AUDIO | ENHANCED / GBA ACCURATE | ENHANCED | Mezcla moderna vs. ruta de audio fiel a GBA. |
| VOLVER AL TÍTULO | CONFIRMAR | — | Vuelve de forma segura a la pantalla de título (con confirmación). La configuración se conserva; el progreso no guardado se comporta como un reinicio normal al título. |
| VERSIÓN | V0.1.1 | — | Indicador de versión (solo lectura). |

## Notas

- Los ajustes se guardan en `/switch/tmc/config.json`. Borrarlo restaura todos
  los valores por defecto (asignaciones incluidas).
- Ningún ajuste de esta lista requiere reiniciar.
