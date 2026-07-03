# Registro de Cambios - Proyecto Editor de Niveles (The Minish Cap PC Port)

Este archivo registra las soluciones y mejoras implementadas en el port de PC de *The Minish Cap* para integrar el editor de niveles y solucionar fallos de estabilidad y visualización (transiciones).

## Nuevas Características

### 0. Incorporación del Editor de Niveles (Direct Painting Overlay)
* **Archivos agregados/afectados:** `port/port_level_editor.cpp`, `port/port_level_editor.h`
* **Descripción:** Se integró un completo editor de mapas interactivo en tiempo real integrado en la propia pantalla del juego. Permite:
  * Pintar baldosas directamente con el ratón (clic izquierdo para pintar, arrastrar para trazar de forma continua).
  * Gotero/Eyedropper para seleccionar baldosas directamente desde la pantalla de juego usando el clic derecho.
  * Atajos de teclado para alternar entre las capas superior (Top) e inferior (Bottom), incrementar/decrementar la ID de baldosa seleccionada, cambiar dinámicamente la iluminación y la música de la sala (BGM y Fight BGM).
  * Guardado instantáneo y persistente en la carpeta local `edited_levels/areaXX_roomXX.bin` con metadatos asociados.

## Cambios y Correcciones Implementadas

### 1. Corrección del Crash al Arrancar (Acceso de Memoria Corrupto)
* **Archivo afectado:** `port/port_level_editor.cpp` (estructura `MapLayer`)
* **Problema:** El port de PC redefinía la estructura `MapLayer` en C++ omitiendo el puntero `void* bgSettings;` al principio de la estructura. Esto causaba un desalineamiento de 8 bytes en todos los offsets posteriores. Cuando el cargador escribía en `gMapBottom.mapData`, corrompía el puntero `bgSettings`, provocando un fallo de segmentación (Access Violation) y crasheo del juego en `UpdateScreenShake()`.
* **Solución:** Se restauró `void* bgSettings;` al inicio de la estructura `MapLayer` en C++, garantizando una alineación idéntica a la del código de C del juego.

### 2. Prevención de Desbordamiento de Índice en el Renderizado de Mapas
* **Archivo afectado:** `src/beanstalkSubtask.c` (función `RenderMapLayerToSubTileMap`)
* **Problema:** En mapas personalizados, ciertos mosaicos devuelven valores fuera de rango o especiales (como `0xFFFF`). Al buscar el índice de metatesela, esto provocaba lecturas fuera de los límites de la matriz `tileIndices` (límites `>= 2048`).
* **Solución:** Se implementó una función auxiliar de validación `GetSafeTileSetIndex` para asegurar que cualquier índice mayor o igual a 2048 sea tratado con seguridad, evitando el desbordamiento de lectura en la tabla de metateselas.

### 3. Solución del Glitch Gráfico de Pantalla Negra en Transiciones de Scroll del Mismo Área
* **Archivo afectado:** `port/port_level_editor.cpp` (función `Port_LevelEditor_OnRoomLoad`)
* **Problema:** Durante las transiciones por scroll dentro de una misma área, el motor del juego utiliza la primera mitad de los buffers de mapa (`gMapBottom.mapData`, `gMapTop.mapData`, y sus homólogos `Original`) para la nueva sala, y la segunda mitad para la sala anterior. Al hacer un `memcpy` del tamaño completo de los buffers (4096 elementos / 64x64), se sobrescribía con ceros la segunda mitad, borrando la sala de la que venía el jugador y haciendo que se viera negra en pantalla.
* **Solución:** 
  1. Se modificó el cargador para que lea y copie de forma **dinámica** solo la cantidad de elementos correspondientes a las filas que ocupa la sala actual: `(gRoomControls.height / 16) * 64` elementos.
  2. Esto permite que las salas grandes se carguen por completo y que las pequeñas dejen intacta la segunda mitad del buffer, preservando la sala adyacente durante el scroll.

### 4. Corrección de la Corrupción de Música (BGM) y Nivel de Luz
* **Archivo afectado:** `port/port_level_editor.cpp` (función `Port_LevelEditor_OnRoomLoad`)
* **Problema:** Al parcializar la lectura de las capas de mapa a solo las filas necesarias, el puntero de lectura del archivo quedaba desalineado. El cargador leía los metadatos del nivel (BGM, música de pelea, nivel de luz) del centro de la capa superior de mosaicos. Esto corrompía el nivel de luz (ej. cargando `129` en lugar de `256`), haciendo que la habitación se viera completamente negra (oscuridad absoluta).
* **Solución:** Se forzó un reposicionamiento absoluto con `input.seekg(16384, std::ios::beg)` al final exacto de la capa superior antes de leer los metadatos. Adicionalmente, se añadió una validación para descartar BGM o niveles de luz corruptos en archivos previamente dañados.

### 5. Renderizado Seguro de VRAM en Carga de Sala
* **Archivo afectado:** `port/port_level_editor.cpp` (función `Port_LevelEditor_OnRoomLoad`)
* **Problema:** Forzar la carga manual a VRAM de forma síncrona mediante `UpdateScrollVram()` durante la transición de carga de sala (cuando las coordenadas de cámara aún no se han sincronizado con la nueva sala) provocaba fallos de alineación en VRAM y pantalla negra.
* **Solución:** Se mantuvieron las reconstrucciones de colisiones y mosaicos en la memoria de trabajo (EWRAM) pero se eliminó el renderizado manual de VRAM síncrono, dejando que el bucle principal de frames del juego actualice la VRAM de forma segura al inicio del renderizado del siguiente frame.

### 6. Integración del Menú Debug de ImGui para el Editor de Mapas
* **Archivo afectado:** `port/port_imgui_menu.cpp`
* **Problema:** Los controles, atajos de teclado y la pestaña interactiva del Editor de Mapas no estaban accesibles ni expuestos en la interfaz gráfica del menú debug de ImGui del port de PC.
* **Solución:** Se implementó una nueva pestaña `"Map Editor"` en la barra de pestañas principal (Ribbon) de ImGui con un checkbox para activar el overlay interactivo de pintura y una lista descriptiva de controles y atajos. Adicionalmente, se conectó el renderizado del overlay a la función del puerto de vídeo en `Port_ImGui_Render()`.
