# NX500 Eye AF v2.8

An optional, experimental autofocus module for the Samsung NX500 running stock firmware 1.12. In photo mode, select **Face Detection → Normal**. With one eligible face the adapter uses the camera's eye detector to place the primary focus region over an eye. The stock camera controls the lens. The adapter uses the native face callback and keeps full-face tracking and metering history in private storage until the final region is ready. With zero faces the stock empty result is published immediately. NX1 and other firmware builds have not been validated.

## Evidence

On one NX500 the restored camera process had the exact release library mapped and reported schema 28. The tester confirmed stable framing with A-AF, including half-press and release. In three consecutive, full-resolution JPEGs, both stored copies of the native AF region agreed. Projecting from the embedded 528 × 352 coordinate system into the 6480 × 4320 images placed one visible eye inside the region in all three. At full scale, iris and eyelid detail was visible. The diagnostic trace ended before the exposures. This evidence establishes the selected AF area in each image; an exact optical focus plane or a general hit rate would require a controlled depth target.

In an ARM replay of 90 stable callbacks, transient full-face publications after the initial eye selection fell from 89 to zero. Across 98 differential frames, the native Y, Cb and Cr statistics and full-face history matched. Another 57 paired scenarios covered movement, brief eye loss, true face loss, hidden eyes, multiple faces and mode changes. Camera, clock and image inputs for the portable regression are modeled.

## Build and test

Use Python 3.9+ and Zig 0.13.0. The replay additionally needs the Python packages `unicorn` (2.x) and `pyelftools`.

```sh
python build.py --zig zig --test
```

The camera library is `out/nxl-eye-af.so`. Only the native face method is exported. The module wraps that method in the existing camera process and binds the original face, eye and UI functions. A desktop build of the release source matched the deployed module byte for byte. Its SHA-256 is `e167e84a6be88b2fc21da13a5952a923ec7a032cb37612a11e047e60dc6c2e6e`.

## Installation and recovery

**Rebuilding fast start changes persistent boot state. A failed rebuild can leave the screen black with the card lamp flashing.** Read the [installation and recovery notes](INSTALL.md) first. The [standalone release](https://github.com/NoireSchwarz/NX500-metol/releases/tag/nx500-eye-af-v2.8) includes the ARM module and the optional, explicitly staged installer. Personal pictures, Samsung firmware and anyone else's hibernation image are excluded. The regular Metol installer and menus are independent of this contribution.

The autofocus sources are licensed under AGPL-3.0-only. The release archive carries separate notices for its installation helpers.
