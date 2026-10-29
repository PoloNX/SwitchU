# haze

MTP server from Atmosphère (`troposphere/haze`, tag 1.11.2), GPLv2.
Copyright (c) Atmosphère-NX. https://github.com/Atmosphere-NX/Atmosphere

Vendored for sMenu's USB file transfer (`projects/sMenu/source/sl/menu/usb`).
Changes from upstream:

- Dropped the standalone app: `main.cpp`, `console_main_loop.hpp`, `gpu_console.c`
  and its shaders.
- `ptp_object_heap.cpp`: a fixed 2 x 4 MiB object heap instead of all free
  memory minus 30 MiB, since it shares the process with the menu; a failed
  allocation fails the session instead of asserting.
- Added `transfer_status.hpp/.cpp` and calls to it in the send, receive and
  delete operations, so the menu can show what is being transferred.
