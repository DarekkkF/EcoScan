# estacion-ambiental-iot

Prototipo IoT con ESP32 que mide temperatura, humedad, luz y gas, y calcula un índice de confort ambiental (0-100).

Desarrollado para la Evaluación Sumativa 1 (Unidad 1: IoT) del ramo TI3042.

## Sensores

| Sensor | Mide | Pin |
|---|---|---|
| DHT22 | Temperatura y humedad | D15 |
| LDR | Luz | GPIO 34 |
| MQ-2 | Gas | GPIO 35 |

## Requisitos

- Visual Studio Code
- Extensión PlatformIO IDE
- Extensión Wokwi Simulator (con licencia gratuita)

Las librerías del código (DHT sensor library, Adafruit Unified Sensor) ya están declaradas en `platformio.ini` y se descargan solas al compilar.

## Cómo correrlo

```bash
pio run
```

Después, en VS Code: `Ctrl+Shift+P` → "Wokwi: Start Simulator" → Play.

## Próximos pasos

- Armar el prototipo físico
- Enviar datos a AWS
- Visualizar en una app Android

