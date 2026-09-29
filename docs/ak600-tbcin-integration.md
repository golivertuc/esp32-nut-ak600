# Integración TBCin AK-600 — Guía completa (ESP32-S3 + KraulerDriver)

> **Estado:** Verificada en producción (2026-09-28)
> **Hardware:** ESP32-S3 DevKitC-1 (N16R8, 8MB flash) + UPS TBCin AK-600 (600VA line-interactive)
> **Firmware:** este repo (esp32-nut v1.6.1 base + `KraulerDriver`)
> **Resultados:** `ups.status: OL` · red 232 V · salida 228 V · batería 13.6 V / 100% · 50 Hz · 34.8 °C

---

## 1. Arquitectura

```
UPS TBCin AK-600
   │  (puerto USB-B)
   │  cable USB-B→A + adaptador A→C
   ▼
ESP32-S3 (puerto Host USB-C, OTG puenteado)
   │  KraulerDriver (Q1/I/F vía string descriptors)
   │  ┌──────────────────────────────┐
   └─┤  NUT Server :3493  (NUT TCP)  │
     │  Web UI :80   (config/OTA)    │
     │  API :80/api/* (JSON)         │
     └──────────────────────────────┘
   │  Wi-Fi "LA FORTALEZA 2.4 Ghz" → 192.168.1.13
   ▼
Watchdog X870 (ups-watchdog.service) → alertas Signal + acciones
```

La placa funciona de forma autónoma: solo necesita **5V por el puerto JTAG** (cargador USB
cualquiera) y Wi-Fi. No requiere PC conectada.

## 2. El chip puente XJB

La AK-600 usa un chip USB-HID "XJB UPS MON V2.0" (VID `0x0001`, PID `0x0000`) que
implementa el protocolo **Megatec/Q1** de NUT (mismo subdriver `krauler` de
`blazer_usb.c`). Particularidades medidas:

| Aspecto | Comportamiento |
|---|---|
| INPUT reports HID | ~7500/min, sin fallas, pero con **usages PDC reutilizados** (valores no estándar) |
| String descriptors de dispositivo (1/2/3 de USB) | **vacíos** (iManufacturer/iProduct/iSerial) |
| String descriptor 3 | respuesta `Q1`: `(in_v fault_v out_v load% freq batt_v temp <8 bits>` |
| String descriptor 12 | respuesta `I`: `#company(15) model(10) version(10)` |
| String descriptor 13 | respuesta `F`: `#in_nom in_cur_nom batt_nom in_freq_nom` |
| GET_REPORT | **no soportado** (solo string descriptors funcionan) |

Esto es lo que el `KraulerDriver` explora: ignora los INPUT reports y lee Q1/I/F
exclusivamente vía string descriptors, igual que NUT.

## 3. Protocolo Q1 (respuestas reales medidas)

```
Q1 → (226.0 195.0 230.0 014 49.0 13.6 30.0 00001000
       │     │       │     │    │      │     │     └─ 8 bits estado
       │     │       │     │    │      │     └─ temperatura °C
       │     │       │     │    │      └─ voltaje batería (V, aquí por celda ×N)
       │     │       │     │    └─ frecuencia Hz
       │     │       │     └─ carga %
       │     │       └─ voltaje de salida V
       │     └─ umbral de falla de red V
       └─ voltaje de red V
```

Bits de estado (MSB primero, como `blazer_status` de NUT):
`[0]` red caída (OB) · `[1]` batería baja (LB) · `[2]` boost/buck · `[3]` selftest
falló · `[4]` tipo offline · `[5]` test en curso · `[6]` shutdown (FSD) · `[7]` beeper

`battery.charge` se estima con la curva de NUT: `100·(V−10.4·nom/12)/(13.0·nom/12)`.
Con batería 12 V y lectura 13.5–13.6 V cargado → **100%** (correcto).

## 4. Implementación en este repo

| Archivo | Rol |
|---|---|
| `lib/USBHostUPS/src/KraulerDriver.h` | Driver: `buildPollLists` (Q1 quick, Q1+I+F full), parseo Q1/I/F, cleanup de keys |
| `lib/USBHostUPS/src/DriverRegistry.cpp` | Registro `{ 0x0001, 0x0000, make<KraulerDriver> }` |
| `docs/adr/0009-krauler-driver-megatec-q1.md` | ADR: decisión, mapeos, lecciones |
| `README.md` | TBCin agregado a la lista de dispositivos soportados |

**Lección clave (ver ADR 0009):** NO usar `QUIRK_NO_GET_REPORT` para este driver.
Ese quirk hace que `GenericDriver::startCycle()` no encole la lista de
`buildPollLists()` — solo los strings de `collectStringRequests()`, que en este chip
están vacíos. Resultado: driver asignado pero **cero peticiones**. El driver resuelve
"no GET_REPORT" en sí mismo: `decodeReport()` es no-op.

## 5. Build y flasheo

### Build (en la PC, PlatformIO)

```bash
git clone git@github.com:golivertuc/esp32-nut-ak600.git && cd esp32-nut-ak600
pio run -e esp32-s3-standard
# → .pio/build/esp32-s3-standard/{firmware,bootloader,partitions}.bin
```

### Opción A — OTA (recomendada, sin cable a la placa)

```bash
curl -F "update=@.pio/build/esp32-s3-standard/firmware.bin" http://<IP_PLACA>/update
# → "OK"; la placa se reinicia sola (~30-40 s)
```

### Opción B — USB (esptool)

```bash
# ESP32-S3 en modo download: BOOT presionado + EN pulsado y soltado
sg dialout -c "python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
  write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin"
```

> **Dual-slot OTA:** el bootloader conmuta el slot activo en `otadata`. Verificar
> post-OTA comparando `sha256sum` del slot activo (0x10000 o 0x400000) con el
> firmware enviado — una OTA `OK` corta a mitad de transferencia puede dejar el slot
> activo viejo.

## 6. Verificación (criterios de aceptación)

```bash
# 1) Estado y lecturas
curl http://<IP>/api/ups-vars
# esperado: "ups.status":"OL" + input.voltage + output.voltage + battery.charge coherente

# 2) Logs
curl http://<IP>/api/logs
# esperado: "Battery = 100% | Status = OL | Voltage = 22x V" repetido y estable

# 3) Estabilidad: 3 lecturas a 15 s separadas, sin reset_reason=EXCEPTION
curl http://<IP>/api/system-status
```

## 7. Hardware: puente OTG de la ESP32-S3 DevKitC-1

Para que el **puerto Host USB-C** entregue 5V al UPS (en vez de recibir VBUS), la
DevKitC-1 requiere puentear los pads **`USB-OTG`** (cara trasera, cerca de la
etiqueta, cuadrante superior derecho). Soldadura de ~2mm, permanente. Verificado
con la APC RS 500 y la AK-600.

Cableado final:
- **JTAG/serial → cargador USB 5V** (alimentación de la placa, independiente)
- **Host USB-C → adaptador A→C → USB-B → AK-600** (el UPS toma 5V de acá)

## 8. Integración con watchdog

El servicio `ups-watchdog` (X870) lee `http://192.168.1.13/api/ups-vars`:
- `ups.status` contiene `OL` → rama `sec-*` activa
- `OB` confirmado (3 lecturas ~30 s) → alerta Signal + acciones de rama OB
- Sin respuesta 5 veces consecutivas → `sec-down` (alerta suprimida por cooldown)
- Respuesta recuperada → `sec-up`

En producción (28/09): `sec-up` 20:17:55, sin caídas desde entonces.
