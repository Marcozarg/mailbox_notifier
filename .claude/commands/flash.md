---
description: Compile and flash the sender (serial USB) or receiver (OTA) firmware
argument-hint: sender | receiver
allowed-tools: Bash, PowerShell, Read, Grep
---

Flash the mailbox firmware. Parse `$ARGUMENTS` for the target: `sender` or `receiver`.
If the argument is missing or unrecognised, stop and ask.

## Constants

```
CLI         = C:\Program Files\Arduino CLI\arduino-cli.exe
BUILD       = C:\Users\marko\AppData\Local\Temp\mailbox_build
SENDER_INO  = O:\GITHUB\mailbox_notifier\firmware\mailbox_sender\mailbox_sender.ino
RECEIVER_INO= O:\GITHUB\mailbox_notifier\firmware\mailbox_receiver\mailbox_receiver.ino
SENDER_SKETCH  = C:\Users\marko\OneDrive\Documents\Arduino\mailbox_sender
RECEIVER_SKETCH= C:\Users\marko\OneDrive\Documents\Arduino\mailbox_receiver
ESPOTA      = C:\Users\marko\AppData\Local\Arduino15\packages\Heltec-esp32\hardware\esp32\3.3.8\tools\espota.exe
RECEIVER_FQBN = Heltec-esp32:esp32:heltec_wifi_lora_32_V3
SENDER_FQBN   = adafruit:avr:feather32u4
OTA_PORT    = 3232
```

---

## Step 1 — Show version being flashed

Read the appropriate `.ino` file and extract the line matching `#define FW_VERSION`.
Report: **Flashing `<target>` — firmware `<version>`**

---

## Step 2 — Discover port

### Receiver
Run:
```powershell
& "C:\Program Files\Arduino CLI\arduino-cli.exe" board list 2>&1
```
Parse for a row with protocol `network`. Extract the IP address. If none found, stop and tell the user the receiver is not visible on the LAN.

### Sender
Run:
```powershell
& "C:\Program Files\Arduino CLI\arduino-cli.exe" board list 2>&1
```
Look for a row that mentions `adafruit:avr:feather32u4` or `Adafruit Feather 32u4` in the board name. Extract the COM port from that row.

If no Adafruit board is found, fall back to **COM7** and warn the user that the port was not auto-detected — ask them to confirm the sender is plugged in and has been reset within the last 20 seconds.

If even COM7 is not listed as any port, stop and tell the user to plug in the sender and reset it, then re-run `/flash sender`.

---

## Step 3 — Compile

```powershell
& "C:\Program Files\Arduino CLI\arduino-cli.exe" compile `
    --fqbn "<FQBN>" `
    --output-dir "C:\Users\marko\AppData\Local\Temp\mailbox_build" `
    "<SKETCH_DIR>" 2>&1
```

On failure: show the full compiler output and stop.
On success: report Flash and RAM usage from the `Sketch uses … bytes` lines.

---

## Step 4 — Upload

### Receiver (OTA via espota.exe)
```powershell
& "C:\Users\marko\AppData\Local\Arduino15\packages\Heltec-esp32\hardware\esp32\3.3.8\tools\espota.exe" `
    -i <IP> -p 3232 --auth="" `
    -f "C:\Users\marko\AppData\Local\Temp\mailbox_build\mailbox_receiver.ino.bin" 2>&1
```
Success = output ends with `[INFO]: Success`. Report success and note the receiver is rebooting (~5 s).

If `No response from device`: tell the user to wait 10 s and retry — the receiver may be mid-operation.

### Sender (serial via arduino-cli)
```powershell
& "C:\Program Files\Arduino CLI\arduino-cli.exe" upload `
    --fqbn "adafruit:avr:feather32u4" `
    --port <COM_PORT> `
    --input-dir "C:\Users\marko\AppData\Local\Temp\mailbox_build" 2>&1
```

Before running upload, remind the user: **the sender must be reset within the last 20 seconds** (the boot upload window). If they haven't reset it yet, tell them to press the reset button now and confirm before proceeding.

Success = no `error` in the output. Report done.
If it fails with a timeout or `avrdude: butterfly_recv()` error, it likely means the upload window expired — ask the user to reset the sender and re-run `/flash sender` (compile is cached, only upload will re-run).
