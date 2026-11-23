# uniwill-ec-hwmon

Read-only Linux hwmon driver for **Uniwill/Tongfang notebook ECs** that are only
reachable through the vendor ACPI-WMI interface.

It exists because many laptop BIOSes never expose the `INOU0000` ACPI device
that the in-tree `uniwill-laptop` driver needs, so `sensors` shows temperatures
but **no fan RPM at all** — the EC still exposes fan tachometers through the
`AcpiTest_MULong` WMI method (`ABBC0F6F-8EA1-11D1-00A0-C90629100000`, method 4).

## What it exposes

| hwmon attribute | Source | Meaning |
| --- | --- | --- |
| `temp1_input` | EC `0x043E` | CPU temperature (°C) |
| `temp2_input` | EC `0x044F` | GPU temperature (°C) |
| `fan1_input` | EC `0x0464..0x0465`, big endian | CPU fan (RPM) |
| `fan2_input` | EC `0x046C..0x046D`, big endian | GPU fan (RPM) |
| `pwm1` | EC `0x075B` | CPU fan PWM (0–255, read-only) |
| `pwm2` | EC `0x075C` | GPU fan PWM (0–255, read-only) |

Once loaded, `sensors` (and any hwmon consumer, e.g. `coolercontrol`) sees a
`uniwill_ec` chip:

```
uniwill_ec-virtual-0
Adapter: Virtual device
CPU fan:     4321 RPM
GPU fan:     4211 RPM
CPU:          +70.0°C
GPU:          +62.0°C
pwm1:             60%
pwm2:             60%
```

## Safety

* **Read-only.** The driver never writes to the EC; it does not change fan
  curves, TDP or power profiles.
* It binds to the unused WMI GUID `ABBC0F6F-…`; it does not touch the EC device
  claimed by the in-tree `uniwill-laptop` driver.
* Reversible: `rmmod uniwill_ec_hwmon` or uninstall the package.
* Do **not** install it together with `clevo-drivers-dkms-git` /
  `tuxedo-drivers`: their `uniwill_wmi` driver binds the same GUID and performs
  EC writes.

## Requirements

* Linux with `wmi` and `hwmon` (any recent mainline/zen kernel).
* DKMS and the headers for the running kernel (`linux-headers`,
  `linux-zen-headers`, `linux-lts-headers`, …).
* A Uniwill/Tongfang EC that exposes the `ABBC0F6F` WMI method. Check with:

```bash
ls /sys/bus/wmi/devices/ | grep ABBC0F6F
ls /sys/bus/acpi/devices/ | grep INOU0000   # if this exists, use the in-tree driver instead
```

## Install

AUR package: `uniwill-ec-hwmon-dkms`

```bash
yay -S uniwill-ec-hwmon-dkms
```

Or build the module manually:

```bash
make
sudo insmod uniwill_ec_hwmon.ko
```

## Uninstall

```bash
sudo pacman -Rns uniwill-ec-hwmon-dkms
```

## Tested hardware

* Avell A62 LIV (Tongfang/Uniwill), i7-10750H, GTX 1650, BIOS N.1.02 —
  no `INOU0000`, WMI `ABBC0F6F` present.
* Should work on other Uniwill/Tongfang/Clevo rebrands exposing the same WMI
  GUID set and the standard Uniwill EC register map.

## License

GPL-2.0-only. See `LICENSE`.
