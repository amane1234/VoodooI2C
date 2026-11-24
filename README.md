[![release](https://img.shields.io/github/release/alexandred/VoodooI2C.svg)](https://github.com/VoodooI2C/VoodooI2C/releases)
[![circleci](https://circleci.com/gh/VoodooI2C/VoodooI2C.svg?style=shield&circle-token=:circle-token)](https://github.com/VoodooI2C/VoodooI2C/releases)
[![Gitter chat](https://img.shields.io/gitter/room/nwjs/nw.js.svg?colorB=ed1965)](https://gitter.im/alexandred/VoodooI2C)

## About this fork (VoodooI2C_APIC)

This fork lets VoodooI2C use **APIC interrupts above `0x2f`**. Upstream VoodooI2C refuses those pins and falls back to GPIO pinning or polling. On machines where the BIOS locks the touchpad's GPIO pad for direct IOxAPIC routing, GPIO pinning cannot work, and the APIC route is the only real interrupt mode.

### Changes compared to upstream

| Change | Where |
|---|---|
| Accept APIC interrupt pins above `0x2f` in `VoodooI2CDeviceNub::validateAPICInterrupt()` | this repository |
| Fix I2C transfer stalls and nub/controller error paths | this repository |
| Polling idle back-off also works with event drivers that do not update `last_multi_touch_event` (for example AlpsHID) | [amane1234/VoodooI2CHID](https://github.com/amane1234/VoodooI2CHID) (submodule) |
| Fix client tracking hang, power-management return values and sensor cleanup | [amane1234/VoodooI2CHID](https://github.com/amane1234/VoodooI2CHID) (submodule) |

### Tested hardware

Dell laptop, Intel Comet Lake-U (PCH-LP), GPIO controller `INT34BB` (`VoodooGPIOCannonLakeLP`).

| Device | ACPI path | ID | Pad | APIC pin | Driver | Result |
|---|---|---|---|---|---|---|
| Touchpad | `\_SB.PCI0.I2C1.TPD1` | `DELL09A0`, Alps HID `0x0488:0x120A` | GPP_B3 | `0x33` | AlpsHID | Works in APIC mode |
| Touchscreen | `\_SB.PCI0.I2C0.TPL0` | `MELF0410` | GPP_C23 | `0x5f` | VoodooI2C touchscreen driver | Works in APIC mode |

Both pins are above `0x2f`, and both work with this fork.

### Why GPIO pinning fails on this machine

A diagnostic build of VoodooGPIO showed that the BIOS fully locks the touchpad pad:

```
pin 35 type 0x8 set_type 1 acpi_mode 0 lock 3 PADCFG0 before 0x40100102 after 0x40100102
```

- `lock 3` is `PAD_LOCKED_FULL`. Writes to `PADCFG0` are ignored, but `intel_gpio_irq_set_type()` does not read the register back, so it still reports success.
- `PADCFG0 = 0x40100102`: Level trigger, `RXINV = 0`, `GPIROUTIOXAPIC = 1`. The BIOS set the pad up for direct IOxAPIC routing.
- The touchpad interrupt is active low. Without `RXINV`, a GPIO interrupt fires while the line idles high. With no finger on the pad, the GPIO controller vector fired about 1,300 times per second (`powermetrics`), the I2C controller saw about 23,000–30,000 interrupts per second, and every read returned an empty 2-byte report (`02 00`). The cursor stuttered badly with Level and with Edge triggering, because the trigger type cannot be changed either.
- Polling worked smoothly but read the device every 5 ms forever with AlpsHID (about 2,000 I2C controller interrupts per second while idle). The VoodooI2CHID change above lets polling back off to 50 ms after 1.5 s without new reports.

If your machine shows the same pattern (GPIO mode storms while idle, polling works, APIC pin above `0x2f`), APIC mode with this fork is worth trying.

### Using APIC mode

The device's `_CRS` must return the APIC interrupt resource (usually `SBFI`) instead of the GPIO one (`SBFG`). Do not override the firmware's GPIO pad variables (`GPDI`, `GPLI`) or mode variables (`TPDM`, `TPLM`), so that `_INI` computes the APIC pin from the firmware values.

Example SSDT for the machine above. It needs the ACPI patches `\_SB.PCI0.I2C1.TPD1._CRS` → `XCRS` and `\_SB.PCI0.I2C0.TPL0._CRS` → `XCRS`:

```asl
DefinitionBlock ("", "SSDT", 2, "ZPSS", "GPI0", 0x00000000)
{
    External (_SB_.PCI0.I2C0.TPL0, DeviceObj)
    External (_SB_.PCI0.I2C0.TPL0.SBFB, BuffObj)
    External (_SB_.PCI0.I2C0.TPL0.SBFI, BuffObj)
    External (_SB_.PCI0.I2C0.TPL0.XCRS, MethodObj)
    External (_SB_.PCI0.I2C1.TPD1, DeviceObj)
    External (_SB_.PCI0.I2C1.TPD1.SBFB, BuffObj)
    External (_SB_.PCI0.I2C1.TPD1.SBFI, BuffObj)
    External (_SB_.PCI0.I2C1.TPD1.XCRS, MethodObj)
    External (GPHD, FieldUnitObj)

    Scope (\)
    {
        If (_OSI ("Darwin"))
        {
            \GPHD = Zero
        }
    }

    Scope (\_SB.PCI0.I2C1.TPD1)
    {
        Method (_CRS, 0, Serialized)
        {
            If (_OSI ("Darwin"))
            {
                Return (ConcatenateResTemplate (SBFB, SBFI))
            }

            Return (XCRS ())
        }
    }

    Scope (\_SB.PCI0.I2C0.TPL0)
    {
        Method (_CRS, 0, Serialized)
        {
            If (_OSI ("Darwin"))
            {
                Return (ConcatenateResTemplate (SBFB, SBFI))
            }

            Return (XCRS ())
        }
    }
}
```

Check the result after booting:

```bash
ioreg -lw0 | grep '"Interrupt Mode"'
```

Each device should report `"APIC"`. The APIC pin is the first byte of the device's `IOInterruptSpecifiers` (for example `<3300000003000000>` is pin `0x33`, level, active low).

Notes:

- If the BIOS did not route a pad to the IOxAPIC, APIC registration can still succeed but no interrupts arrive, and the device does not respond. There is no automatic fallback to polling in that case.
- Upstream VoodooI2C releases do not contain the APIC change. Replacing these kexts with upstream ones brings back the `0x2f` limit and polling.

### Building

macOS with Xcode is required. Clone `main` with submodules, so that the modified VoodooI2CHID is included:

```bash
git clone --recursive -b main https://github.com/amane1234/VoodooI2C_APIC.git
```

```bash
cd VoodooI2C_APIC
```

```bash
git clone --depth 1 https://github.com/acidanthera/MacKernelSDK.git
```

```bash
src=$(/usr/bin/curl -Lfs https://raw.githubusercontent.com/acidanthera/VoodooInput/master/VoodooInput/Scripts/bootstrap.sh) && eval "$src" && mv VoodooInput Dependencies
```

```bash
xcodebuild -workspace VoodooI2C.xcworkspace -scheme VoodooI2C -configuration Release -sdk macosx CODE_SIGNING_ALLOWED=NO CONFIGURATION_BUILD_DIR="$PWD/build/Release" build
```

The kexts are written to `build/Release/`. The GitHub Actions workflow (`.github/workflows/build.yml`) builds the same way on pushes to `main`.

## What is VoodooI2C?

VoodooI2C is a project consisting of macOS kernel extensions that add support for I2C bus devices. The project is split into two main components: the **core** extension and various other **satellite** extensions.

### The Core

The core is the `VoodooI2C.kext` kernel extension. This kext is intended to be installed by anyone whose computer requires some form of I2C support. It consists of I2C controller drivers and is responsible for publishing device nubs to the IOService plane.

### The Satellites

The satellites are a collection of various kernel extensions that implement support for a specific type of I2C device. An example of a satellite kext is `VoodooI2CHID.kext` which adds support for I2C-HID devices. Usually a user will install one satellite kext per class of I2C device.

## Current Status

The following Intel I2C controllers are fully supported:

1. `INT33C2` and `INT33C3` - Haswell era
2. `INT3432` and `INT3433` - Broadwell era
3. `pci8086,9d60`, `pci8086,9d61`, `pci8086,9d62` and `pci8086,9d63` - Skylake era
4. `pci8086,a160`, `pci8086,a161`, `pci8086,a162` and `pci8086,a163` - Kaby Lake era
5. `pci8086,9de8`, `pci8086,9de9`, `pci8086,9dea` and `pci8086,9deb` - Cannon Lake/Whiskey Lake era
6. `pci8086,a368`, `pci8086,a369`, `pci8086,a36a` and `pci8086,a36b` - Coffee Lake era
7. `pci8086,2e8`, `pci8086,2e9`, `pci8086,2ea`, `pci8086,2eb`, `pci8086,6e8`, `pci8086,6e9`, `pci8086,6ea` and `pci8086,6eb`- Comet Lake era
8. `pci8086,34e8`, `pci8086,34e9`, `pci8086,34ea` and `pci8086,34eb` - Ice Lake era
9. `pci8086,a0e8`, `pci8086,a0e9`, `pci8086,a0ea`, `pci8086,a0eb`, `pci8086,43e8`, `pci8086,43e9`, `pci8086,43ea` and `pci8086,43eb` - Tiger Lake era
10. `pci8086,51e8`, `pci8086,51e9`, `pci8086,51ea`, `pci8086,51eb`, `pci8086,7acc`, `pci8086,7acd`, `pci8086,7ace`, `pci8086,7acf`, `pci8086,54e8`, `pci8086,54e9`, `pci8086,54ea` and `pci8086,54eb` - Alder Lake era
11. `pci8086,7a4c`, `pci8086,7a4d`, `pci8086,7a4e` and `pci8086,7a4f` - Raptor Lake era

The following device classes are fully supported:

1. I2C-HID devices
2. ELAN devices
3. FTE devices

Note that there is sometimes an overlap between device classes. For example, some ELAN devices may also be I2C-HID devices.

## Releases

The latest version is ![release](https://img.shields.io/github/release/alexandred/VoodooI2C.svg) and can be downloaded on the [release page](https://github.com/alexandred/VoodooI2C/releases).

## Compatibility

Please check the [compatibility page](https://github.com/alexandred/VoodooI2C/wiki/Compatibility) on the VoodooI2C wiki to find out if your device is compatible. If it is not on the list but you still suspect VoodooI2C may work for you, contact us on our [Gitter page](http://gitter.im/alexandred/VoodooI2C).

## Documentation and Troubleshooting

Please visit the [documentation site](https://voodooi2c.github.io/) for further information how to install and troubleshoot VoodooI2C. 

## License

This program is protected by the GPL license. Please refer to the `LICENSE.txt` file for more information

## Contributing

We are looking for competent C++, OS X kernel, Linux kernel, I2C, HID etc developers to help improve this project! Here are the guidelines for contributing:

* Fork this repository and clone to your local machine
* Create a new feature branch and add commits
* Push your feature branch to your fork
* Submit a pull request upstream
