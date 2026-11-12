# uLoader (vendored)

This directory contains the uLoader launcher from
[uLaunch](https://github.com/XorTroll/uLaunch), vendored into SwitchU so that
the HOME menu can start `.nro` files directly in full-RAM application mode
without forwarders.

Changes from upstream:

- The uCommon, libnx-ext and JSON dependencies were removed. The loader only
  needs libnx.
- Logging is written to `sdmc:/switch/SwitchU/uloader.log`.
- The `hbloader` setsys heap settings are optional; if they are missing the
  loader uses the full process heap.
- The launch-parameter layout is duplicated (not included) from
  `projects/common/include/switchu/smi_protocol.hpp`, with static assertions on
  every offset and the total size, so the menu, daemon and loader cannot
  silently disagree.

Licensing:

- Upstream uLaunch, including this uLoader, is GPL-2.0. See `LICENSE`.
- The loader is derived from nx-hbloader; see `LICENSE.nx-hbloader.md`.
- uLoader is a separate executable from SwitchU. It shares no headers or code
  with SwitchU, so the two programs can be distributed together without mixing
  their licenses.

Build both ExeFS variants with xmake's `switchu-uloader` target. They are
installed to `sdmc:/switch/SwitchU/bin/uLoader/{applet,application}/`.
