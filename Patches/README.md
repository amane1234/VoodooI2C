# Patches

Patches for submodules that this fork does not have its own copy of. `.github/workflows/build.yml` applies every `VoodooGPIO-*.patch` here to `Dependencies/VoodooGPIO` before building.

## VoodooGPIO-wake-reprogram.patch

Re-programs GPIO interrupt pads after S3/S4 resume. Applies to VoodooGPIO `44fc744` (the submodule commit; same tree as the earlier `d415406`).

On S3/S4 resume the BIOS runs before macOS and reprograms GPIO pads, often to its own defaults (for example ACPI ownership and APIC-routing polarity when the BIOS "Interrupt mode" setting is APIC). VoodooGPIO restored a snapshot taken at suspend, but:

- the GPIO controller and the I2C devices are in different branches of the power tree, so a client could re-enable its interrupt before VoodooGPIO resumed, and the snapshot (taken after the client had disabled it) then masked it again;
- `intel_gpio_irq_set_type()` never read the pad back, so a write ignored by a locked pad was reported as success;
- `enableInterrupt()` did not check the pad configuration.

Changes:

- Track per pin whether the client has the interrupt enabled, and restore the enable bit from that state on resume instead of from the snapshot.
- After resume, re-take host ownership and re-apply the IRQ type of every registered pin, then read it back and log the result.
- `enableInterrupt()` re-applies the IRQ type if the pad no longer matches (one register read when nothing changed).
- `intel_gpio_irq_set_type()` reads the pad back and returns failure, with a log line, if the write was ignored (pad locked by the BIOS).

Log lines to look for after wake:

```bash
sudo dmesg | grep -E "Pad [0-9]+ type|ignored IRQ type|Woke up" | tail -10
```

- `reprogrammed`: the BIOS had changed the pad and VoodooGPIO restored it.
- `unchanged`: the pad was already correct.
- `reprogram FAILED, pad locked?` or `ignored IRQ type`: the BIOS locks the pad on resume. A driver cannot fix that; use APIC mode instead (see the main README).

Status: syntax-checked against MacKernelSDK headers only. Not yet tested on hardware.

## Local build with the patches

```bash
git clone --recursive -b gpio-wake-reprogram https://github.com/amane1234/VoodooI2C_APIC.git
```

```bash
cd VoodooI2C_APIC && git -C Dependencies/VoodooGPIO apply ../../Patches/VoodooGPIO-wake-reprogram.patch
```

Then follow the "Building" steps in the main README.
