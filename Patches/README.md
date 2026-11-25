# Patches

Patches for submodules that this fork does not have its own copy of. `.github/workflows/build.yml` applies every `VoodooGPIO-*.patch` here to `Dependencies/VoodooGPIO` before building.

## VoodooGPIO-wake-reprogram.patch

Re-programs GPIO interrupt pads after S3/S4 resume. Applies to VoodooGPIO `44fc744` (the submodule commit; same tree as the earlier `d415406`).

On S3/S4 resume the BIOS runs before macOS and reprograms GPIO pads, often to its own defaults (for example ACPI ownership and APIC-routing polarity when the BIOS "Interrupt mode" setting is APIC). VoodooGPIO restored a snapshot taken at suspend, but:

- the GPIO controller and the I2C devices are in different branches of the power tree, so a client could re-enable its interrupt before VoodooGPIO resumed, and the snapshot (taken after the client had disabled it) then masked it again;
- `intel_gpio_irq_set_type()` never read the pad back, so a write ignored by a locked pad was reported as success;
- `enableInterrupt()` did not check the pad configuration.

The GPIO work loop serializes client operations, interrupt delivery and power transitions. Each pin keeps its requested enable state separately from its hardware state. The shared parent line is enabled for the first armed pin and explicitly disabled for the last. Normal re-enables reuse the verified configuration. Sleep invalidates verification.

On resume the driver restores the interrupt-enable snapshot only for unmanaged pins. A requested GPIO pin must pass PAD_OWN, HOSTSW_OWN and IRQ-type checks before it can be unmasked. A locked or unowned pin stays masked; other pins continue. An explicit enable can retry later. Look for `host ownership unavailable`, `IRQ type ... rejected` or `remains masked after wake` in the kernel log.

Hardware validation is pending.

## Local build with the patches

```bash
git clone --recursive -b gpio-wake-reprogram https://github.com/amane1234/VoodooI2C_APIC.git
```

```bash
cd VoodooI2C_APIC && git -C Dependencies/VoodooGPIO apply ../../Patches/VoodooGPIO-wake-reprogram.patch
```

Then follow the "Building" steps in the main README.
