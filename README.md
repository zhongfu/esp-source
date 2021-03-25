# esp-source

Sling + Sinter on ESP32. Probably not production quality yet, but ehh yolo

## Building

Export the ESP-IDF envs:

```
. path/to/esp-idf/export.sh
```

Then build:

```
idf.py build
```

## Flashing

```
idf.py flash -p /dev/ttyUSBx
```

## Usage

### WiFi configuration

On the first boot (or after 10 WiFi connection failures, or on button press on GPIO 37), esp-source
will go into AP mode, with SSID "esp-source xxxx" (where xxxx is the last two octets of the
**base** MAC addr) and passphrase being the base MAC addr in lowercase.

In essence, if your ESP32 has the **base** MAC address `1F:0E:93:8B:10:F4`, you'd want to look out
for SSID `esp-source 10f4` and connect to that network with passphrase `1f0e938b10f4`.

After connecting, head over to 192.168.4.1 and configure your WiFi credentials.

The page you'll see will have:
- the generated Sling secret
- WiFi configuration
  - SSID: self explanatory
  - Authmode: Open, WPA2-Personal, WPA2-Enterprise/802.1x with PEAP/MSCHAPv2. WEP/WPA is technically
    possible, but we don't allow that because you shouldn't be using WEP/WPA in current year.
    - WPA3 should work as well, but I haven't had the chance to test it out.
  - Validate CA: only shows up with WPA2-Enterprise. At the moment, you can't upload a new CA cert,
    but it'll use the DigiCert Global Root CA by default (for auth01.nw.nus.edu.sg).
  - Identity: only shows up with WPA2-Enterprise. Basically, the username for 802.1x.
  - Password: shows up with WPA2-Personal or WPA2-Enterprise. Self-explanatory.

After submitting, you'd have to restart the ESP32 because I'm too lazy to make it Just Work^tm.

### Running Source programs

Just use it like an EV3 bot. Add a new bot with the Sling secret you (hopefully) copied earlier and
run your program.
