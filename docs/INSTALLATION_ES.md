# Instalación (v0.1.2)

*[English version → INSTALLATION.md](INSTALLATION.md)*

## Requisitos

- Una Nintendo Switch capaz de ejecutar homebrew (probado con Atmosphère).
- Tu ROM **propia y obtenida legalmente** de *The Minish Cap* versión **USA**
  (`.gba`).
- Una tarjeta SD.

**Este proyecto no incluye ninguna ROM, no proporciona enlaces de descarga y
no distribuye contenido del juego con derechos de autor.** Vuelca tu propio
cartucho u obtén el juego legalmente.

## Estructura en la SD

```
/switch/tmc/
    tmc_aleks_ultimate_nx_v0.1.2.nro    ← el port
    baserom.gba                         ← tu ROM USA (vale cualquier nombre)
```

1. Crea `/switch/tmc/` en la SD si no existe.
2. Copia allí el NRO.
3. Copia tu ROM USA en la misma carpeta.

### Nombre de la ROM

El nombre del archivo **no** importa: el cargador examina `/switch/tmc/` en
busca de cualquier `.gba` e identifica *The Minish Cap* por su cabecera
interna (`BZME` = USA). `baserom.gba` es el nombre convencional y se
encuentra primero. Si hay varias ROMs de Minish Cap en la carpeta, gana la
primera coincidencia — deja solo una para evitar ambigüedades.

La v0.1.2 está orientada a la versión **USA**. El volcado USA de referencia es
SHA-1 `b4bd50e4131b027c334547b4524e2dbbd4227130` (no se comprueba en tiempo de
ejecución, pero es con el que se compiló y probó esta versión).

## Primer arranque

- Lánzalo desde el Homebrew Menu. Se recomienda el **modo aplicación** (lanzar
  sobre un título completo); el modo applet tiene menos memoria y no es la
  configuración probada.
- El primer arranque prepara una caché de recursos a partir de tu ROM en
  `/switch/tmc/assets/`, con una barra de progreso. **Puede tardar alrededor de
  un minuto o más según la tarjeta SD** — déjalo terminar. Ocurre una sola vez;
  los siguientes arranques la reutilizan (unos 13–14 segundos hasta el título
  en el hardware probado).
- Después el juego arranca a la pantalla de título.

## Archivos que crea el port

| Ruta | Propósito |
|---|---|
| `/switch/tmc/assets/` | caché de recursos generada desde **tu** ROM (se puede borrar; se regenera) |
| `/switch/tmc/config.json` | ajustes y asignaciones de controles |
| `/switch/tmc/tmc.sav` | el guardado normal del juego |
| `/switch/tmc/autosave.bin` | el autoguardado gestionado por el port (separado del guardado del juego) |
| `/switch/tmc/tmc.softslots` | asignaciones de los atajos X/Y/ZL/ZR |
| `/switch/tmc/ra_badges/` | caché de insignias de RetroAchievements |
| `/switch/tmc/ra_token` | tu token de RetroAchievements (solo si inicias sesión; CERRAR SESIÓN lo elimina) |

Borrar `assets/`, `config.json` o `ra_badges/` es siempre seguro — se
regeneran o restablecen. `tmc.sav` y `autosave.bin` son tu progreso: haz copia
antes de experimentar.

## Actualizar desde una versión anterior

Sustituye el NRO. Partidas, configuración y caché se conservan. Si una versión
futura cambia el formato de la caché, se reconstruirá sola.

## Desinstalar

Borra `/switch/tmc/`. (Copia antes `tmc.sav` / `autosave.bin` si quieres
conservar tu progreso.)
