# SwitchU-MTP (optional)

USB MTP server so a PC can mount the SD card while SwitchU HOME is open.

This binary **vendors Atmosphère haze (GPLv2)**. It is **not** linked into
`SwitchU-menu`. Install it only if you want the feature.

## Install

Copy onto the SD card:

```
atmosphere/contents/01000000000008FE/exefs.nsp
atmosphere/contents/01000000000008FE/flags/boot2.flag
```

(`build.ps1` / xmake install place these under `sd_out/optional/switchu-mtp/`.)

Reboot. With the sysmodule present, SwitchU creates
`sdmc:/config/SwitchU/mtp_enable` while the menu runs and removes it on exit so
USB is released for games.

## Uninstall

Delete `atmosphere/contents/01000000000008FE/` and reboot.

## License

GPLv2 — see haze headers and [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere).
