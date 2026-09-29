# FLASHING.md — Cómo flashear la ESP32-S3 con este firmware

Guía completa para que cualquier persona pueda llevar este proyecto a una
placa ESP32-S3 en cero. Cubre tres caminos: **firmware precompilado** (lo más
sencillo), **build desde código fuente** y **setup Wi-Fi tras el primer
encendido**.

## 0. ¿Qué necesito?

| Elemento | Detalle |
|----------|---------|
| Placa | **ESP32-S3 DevKitC-1 N16R8** (u otra ESP32-S3 con 8 MB flash y **USB OTG Host**) |
| UPS | Cualquier modelo soportado (ver `README.md`), p.ej. TBCin AK-600, APC RS 500 |
| Cableado | Puerto Host de la placa → OTG → cable USB de la UPS. Puerto COM/UART → cargador USB 5V |
| PC (solo para flashear) | Linux, macOS o Windows con WSL. `python3` con `esptool` (`pip3 install esptool`) |

> La ESP32-S3 es obligatoria: necesita USB Host nativo. ESP32 clásico o C3 no sirven.
> Si tu placa genérica tiene dos USB-C, soldá el puente **USB-OTG** en el reverso
> para que el puerto Host entregue 5V a la UPS (ver `README.md`).

## 1. Opción A — Firmware precompilado (recomendado)

Los binarios están en [`esp32-s3-standard/`](esp32-s3-standard/):

- `bootloader.bin` (18 KB)
- `partitions.bin` (3 KB)
- `firmware.bin` (1.5 MB)
- `SHA256SUMS` — verificá la integridad: `sha256sum -c SHA256SUMS`

### 1.1 Poné la placa en modo download (ESP32-S3)

1. Con la placa encendida (o apagada):
2. **Mantené presionado BOOT**, tocá **RESET** una vez, y **soltá BOOT**.
3. El LED de status se apaga/parpadea distinto: la placa está esperando esptool.
4. En Linux aparece como `/dev/ttyACM0` (driver CH340) o `/dev/ttyUSB*`.
   Verificá: `lsusb` y `dmesg | tail`.

> ¿No aparece el puerto? Probá otro cable (muchos cables solo llevan 5V),
> otro puerto USB, o `sudo usermod -aG dialout $USER` + relogin.

### 1.2 Flaseo (Linux)

```bash
cd firmware/esp32-s3-standard
sha256sum -c SHA256SUMS          # sanity check
sg dialout -c "python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
    write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin"
```

Salida esperada (últimas líneas):

```
Hash of data verified.
Leaving...
Hard resetting via reset_pin...
```

### 1.3 macOS

```bash
cd firmware/esp32-s3-standard
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem* --baud 460800 \
    write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

Si `python3 -m esptool` no existe: `pip3 install esptool` o `brew install esptool`.

### 1.4 Windows (WSL)

```bash
cd firmware/esp32-s3-standard
python3 -m esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 \
    write-flash 0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
```

El puerto debe verse desde WSL (`\\.\COM5` se mapea a `/dev/ttyACM0`).

## 2. Opción B — Build desde fuente

Requisitos: [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html)
o VS Code con la extensión PlatformIO.

```bash
git clone https://github.com/golivertuc/esp32-nut-ak600.git
cd esp32-nut-ak600
pio run -e esp32-s3-standard
pio run -e esp32-s3-standard -t upload --upload-port /dev/ttyACM0
```

Los binarios quedan en `.pio/build/esp32-s3-standard/` (los mismos que
publicamos en `firmware/esp32-s3-standard/`).

Tests de unidades sin hardware:

```bash
pio test -e native
```

## 3. Primer encendido — configuración Wi-Fi

1. Encendé la placa. Al no tener Wi-Fi guardado, levanta un Access Point:
   - **SSID:** `NUT_ESP32_Config`
   - **Password:** `12345678`
2. Conectá un celular o PC a ese Wi-Fi.
3. El captive portal se abre solo; si no, navegá a `http://192.168.4.1`.
4. Ingresá las credenciales de tu red Wi-Fi y el usuario/contraseña del servidor NUT.
5. Guardá; la placa reinicia y se conecta a tu red.

> Para forzar el modo AP manualmente: encendé la placa y **desconectala dentro
> de 3 segundos**. Al encender de nuevo arranca en Access Point.

## 4. Verificación

- **Dashboard web:** navegá a la IP que le asignó tu DHCP (buscala en tu router
  o con `arp -a`). Ahí tenés la batería, voltaje, carga, etc.
- **Cliente NUT:** desde Home Assistant, NAS o cualquier máquina:

  ```bash
  upsc <usuario>:<password>@<IP-ESP32>:3493@<IP-ESP32>
  ```

- **Consola serial (opcional):** `python3 -m serial.tools.miniterm /dev/ttyACM0 115200`.

## 5. Solución de problemas

| Síntoma | Causa probable / fix |
|---------|----------------------|
| `A fatal error occurred: Timed out waiting for packet header` | Placa no está en modo download. Repetí BOOT+RESET. Cable solo-5V → cambiá cable. |
| `No serial devices found` | `lsusb` no muestra `1a86:55d3` (CH340) ni `303a:1001` (USB-Serial-JTAG). Probá otro puerto/cable. |
| `Permission denied` al abrir el puerto | `sudo usermod -aG dialout $USER` + relogin, o usá `sg dialout -c "..."`. |
| El dashboard carga pero no hay datos de UPS | Verificá que la UPS está conectada al puerto **Host** (OTG), no al COM. Revisá `ups.model` en el dashboard (si es "Unknown Model" el chip no responde el descriptor de string 12; las lecturas siguen funcionando). |
| Flash falla a mitad de `firmware.bin` | Bajá el baud: `--baud 115200`. |

## 6. Actualización futura (OTA)

La app soporta **web-installer** (ver `README.md`): actualizás la firmware sin
cable desde el dashboard. El archivo `firmware.bin` de este repo se puede
usar directamente como payload de esa actualización.
