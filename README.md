# ESP32 Sinter example

Runs SVML programs, that's about it.

## Building

First, export the ESP-IDF envs:

```
. path/to/esp-idf/export.sh
```

Then build:

```
idf.py build
```

## Flashing

```
idf.py flash
```

## Usage

```
./run.sh /dev/ttyUSBx main/lib/sinter/test_programs/equals.svm
```
