---
type: decision
title: "Krauler (Megatec Q1) Driver via HID String Descriptors"
description: "Documento ADR: KraulerDriver para UPS con chip XJB 'UPS MON V2.0' (TBCin AK-600 y similares) — lectura vía string descriptors Q1/I/F"
tags: [adr, decision, krauler, megatec, q1]
---
# Krauler (Megatec Q1) Driver via HID String Descriptors

* **ADR ID:** 0009
* **Status:** Accepted
* **Date:** 2026-09-28
* **Authors:** @g0newiped

## Context and Problem Statement

La UPS **TBCin AK-600** (600VA line-interactive) se conecta por USB a un chip puente
"XJB UPS MON V2.0" (VID `0x0001`, PID `0x0000`) que implementa el protocolo
**Megatec/Q1** ("Krauler") de NUT. En esp32-nut el firmware la detectaba como
"Unknown Model" con `ups.status: Unknown`:

1. El chip enumera correctamente como HID y emite ~7500 INPUT reports/min sin fallas,
   pero esos reports **no llevan valores PDC estándar**: el descriptor reutiliza
   usages `0x84`/`0x85` (UPS.PowerSummary, UPS.BatterySystem) con escalas no estándar,
   por lo que la decodificación HID mapeaba los valores a variables NUT equivocadas
   (ej. 226 V → `input.transfer.low`, 13.6 V batería → `input.frequency`).
2. Los string descriptors de dispositivo (iManufacturer/iProduct) vienen vacíos,
   por lo que el registro por VID/PID era el único camino de identificación.
3. El driver Krauler del subdriver de referencia NUT (`blazer_usb.c`) demuestra que el
   chip **sí** responde comandos Megatec (`Q1`, `I`, `F`) expuestos como string
   descriptors HID (índices 3, 12 y 13), con las respuestas canónicas:
   * `Q1` → `(226.0 195.0 226.0 014 49.0 13.6 30.0 00001000`
   * `I`  → `#-------------   ------     VT12046Q`
   * `F`  → `#230.0 000 024.0 50.0`

## Considered Options

* **Option 1: Corregir el mapeo HID (decodeReport).** Añadir entradas a la tabla de
  usages de `GenericDriver` con los paths no estándar del chip.
* **Option 2: Driver dedicado por string descriptors (como NUT `blazer_usb`).** Un
  `KraulerDriver` que no depende de la decodificación HID en absoluto: encola
  `STRING_ITEM` 3/12/13, ignora los INPUT reports y parsea las respuestas Q1/I/F.

## Decision Outcome

Chosen option: "**Option 2: Driver dedicado**", porque reproduce exactamente el
comportamiento del driver de referencia NUT (source of truth), aísla las
particularidades del chip, y no contamina la tabla de mapeos estándar que comparten
los drivers APC/Eaton/CyberPower/Powercom.

Implementación: `lib/USBHostUPS/src/KraulerDriver.h` + registro
`{ 0x0001, 0x0000, make<KraulerDriver> }` en `DriverRegistry.cpp`.

## Details

### Mapeo Q1 (string 3)

| Campo (orden de respuesta) | Variable NUT |
|---|---|
| `in_v` | `input.voltage` |
| `fault_v` | `input.voltage.fault` |
| `out_v` | `output.voltage` |
| `load %` | `ups.load` |
| `freq` | `input.frequency` |
| `batt_v` | `battery.voltage` (×multiplier 6/4/3/2/1 para caer en [0.8, 1.25]× nominal) |
| `temp` | `ups.temperature` |
| 8 bits de estado | `ups.status.ac_present/discharging/battery_low/shutdown_imminent`, `ups.beeper.status` |

Bits de estado (MSB primero, como NUT `blazer_status`):
`[0]` utility fail → OB, `[1]` battery low → LB, `[2]` boost/buck,
`[3]` selftest failed, `[4]` standby type, `[5]` test in progress,
`[6]` shutdown active → FSD, `[7]` beeper on.

`battery.charge` se estima con la curva por defecto de NUT
(`10.4·nom/12` a `13.0·nom/12`), que para 12 V da 10.4–13.0 V, coherente con la
lectura real de 13.5–13.6 V cargado.

### Mapeo I (string 12) y F (string 13)

* `I`: 15+10+10 chars → `ups.mfr` / `ups.model` / `ups.firmware`.
* `F`: 4 floats → `input.voltage.nominal`, `input.current.nominal`,
  `battery.voltage.nominal`, `input.frequency.nominal`.

### Quirk NO_GET_REPORT (lección clave)

La primera versión cargó `QUIRK_NO_GET_REPORT` para VID `0x0001`. Con ese quirk,
`GenericDriver::startCycle()` **no encola** la lista de polls de `buildPollLists()`;
solo encola los strings de `collectStringRequests()` (iManufacturer/iProduct/iSerial),
que en este chip están vacíos. Resultado: driver asignado pero **cero peticiones de
comando**, y el quirk también impide que `decodeReport` reciba los INPUT reports
(proprietarios de todos modos).

Regla de diseño: un driver que depende de `buildPollLists()` para sus strings de
comando **no debe** llevar `QUIRK_NO_GET_REPORT`; el quirk solo debe usarse cuando el
driver ignora por completo los reports HID (KraulerDriver cumple eso vía
`decodeReport()` no-op, pero necesita que la base encole sus `STRING_ITEM`).

## Consequences

### Positive
* `ups.status: OL` real con lecturas coherentes (232 V red, 228 V salida, 13.6 V / 100 % batería, 50 Hz, 34.8 °C),
  verificado 3 muestras estables a 15 s y consumido por el watchdog de UPS.
* Cero cambios en el stack HID: los demás drivers (APC, Eaton, CyberPower, Powercom)
  no se tocan.
* Extensible: otros UPS con chip "XJB UPS MON" (VID `0x0001`) — p. ej. las Krauler
  UP-M500VA de la tabla de NUT — quedan cubiertos por el mismo driver.

### Negative
* `ups.model` puede seguir vacío si el chip no responde el string 12
  (se ve "Unknown Model" en system-status, aunque las lecturas sean correctas).
  Mitigación: el parseo `I` setea `ups.model` si responde.
* `battery.charge` es una estimación por voltaje (sin runtimecal), como en NUT.

## Impact on Agent Implementation

* **Nueva regla de driver**: para protocolos Megatec/Q1 expuestos por string
  descriptors, heredar de `GenericDriver` y sobrescribir `buildPollLists`,
  `collectStringRequests` (vacio), `decodeReport` (no-op) y `parseStringDescriptor`.
* **Anti-patrón**: no combinar `QUIRK_NO_GET_REPORT` con drivers que encolan strings
  de comando vía `buildPollLists()`; se pierden las peticiones.
* **Verificación de aceptación**: `/api/ups-vars` debe mostrar `ups.status: OL/OB`,
  `input.voltage`, `output.voltage`, `battery.charge` coherentes; log con
  `Battery = N% | Status = OL | Voltage = ...`.
