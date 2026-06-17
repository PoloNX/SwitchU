<div align="center">
    <h1>SwitchU</h1>
    <p>A Wii U-style custom home menu replacement for Nintendo Switch</p>
</div>

<p align="center">
  <a rel="LICENSE" href="https://github.com/PoloNX/SwitchU/blob/master/LICENSE">
    <img src="https://img.shields.io/static/v1?label=license&message=GPLV3&labelColor=111111&color=0057da&style=for-the-badge" alt="License">
  </a>
  <a rel="VERSION" href="https://github.com/PoloNX/SwitchU/releases">
    <img src="https://img.shields.io/static/v1?label=version&message=1.0.0&labelColor=111111&color=06f&style=for-the-badge" alt="Version">
  </a>
  <a rel="BUILD" href="https://github.com/PoloNX/SwitchU/actions">
      <img src="https://img.shields.io/github/actions/workflow/status/PoloNX/SwitchU/switch.yml?branch=master &labelColor=111111&color=06f&style=for-the-badge" alt=Build>
  </a>
</p>

---

- [Features](#features)
- [Screenshots](#screenshots)
- [How to build](#how-to-build)
- [Help me](#help-me)
- [Credits](#credits)
- [License](#license)


## Screenshots

![](./screenshots/1.jpg)

<details>
  <summary><b>More screenshots</b></summary>

![](./screenshots/2.jpg)
![](./screenshots/3.jpg)
![](./screenshots/4.jpg)
![](./screenshots/5.jpg)

</details>

## How to build

### Requirements

- [devkitPro](https://devkitpro.org/wiki/Getting_Started)
- [Xmake](https://xmake.io/#/)

### Clone

```bash
git clone --recursive https://github.com/PoloNX/SwitchU
cd SwitchU
```

### Build (production daemon + external menu mode)

```bash
xmake f -p cross --toolchain=devkita64 --homebrew=n --backend=deko3d
xmake
```

### Build (homebrew .nro mode)

```bash
xmake f -p cross --toolchain=devkita64 --homebrew=y --backend=deko3d
xmake
```

### Clean

```bash
xmake clean
```

Build outputs are generated under `build/cross/aarch64/<mode>/`.

## Know issues
- Some settings are not implemented yet
- Current icons are very ugly, feel free to replace them with better ones
- You may experience somme crash when using a lot of sysmodules. I tried to reduce the ram usage but I still get some instability when launching a game.

## TODO
- Add a pannel when pressing + on a game to show more information about it
- Add video in background for the home menu
- Add a steamgriddb integration to get game banners and backgrounds

## Help me

If you want to help, open an issue when you find a bug and open a pull request if you have a fix.

## Credits

- Thanks to [Xortroll](https://github.com/Xortroll) for the help and for [uLaunch](https://github.com/Xortroll/uLaunch) which inspired this project a lot

## License

This project is licensed under the GNU General Public License v3.0. See the [LICENSE](https://github.com/PoloNX/SwitchU/blob/master/LICENSE) file for details.
