# Third-Party Notices

This project incorporates or is derived in part from code and data produced by
other open-source projects. This file lists those sources, their licenses, and
what was used from each, per the license terms that require attribution.

---

## 1. ft8_lib (FT8/FT4 encoder/decoder)

- **Project:** ft8_lib
- **Author:** Karlis Goba, YL3JG
- **Source:** https://github.com/kgoba/ft8_lib
- **License:** MIT License
- **Used for:** FT8 decoding on the DeepSDR 101 firmware (candidate search,
  LDPC decode, message unpacking, Costas sync). Adapted for embedded use:
  reduced oversampling (TIME_OSR=1, FREQ_OSR=2) after A/B testing against
  ft8_lib's own WAV test corpus, and integrated into the firmware's own
  audio pipeline, RTC-driven slot scheduling, and RAM-lending/union memory
  scheme.
- **Compatibility note:** MIT is permissive and imposes no restriction
  beyond retaining the copyright/license notice, so it combines cleanly
  with this project's GPL-3.0 license (as it did before, under CC BY-NC 4.0).

> MIT License notice (reproduce verbatim if redistributing ft8_lib source or
> binaries derived from it):
>
> Copyright (c) Karlis Goba
>
> Permission is hereby granted, free of charge, to any person obtaining a
> copy of this software and associated documentation files (the
> "Software"), to deal in the Software without restriction, including
> without limitation the rights to use, copy, modify, merge, publish,
> distribute, sublicense, and/or sell copies of the Software, and to
> permit persons to whom the Software is furnished to do so, subject to
> the following conditions: the above copyright notice and this
> permission notice shall be included in all copies or substantial
> portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
> WARRANTY OF ANY KIND.
>
> (Confirm exact wording against the LICENSE file in the ft8_lib
> repository before shipping — the summary above should not be copy-pasted
> as a substitute for that file.)

ft8_lib itself credits Robert Morris (AB1HL) for algorithmic groundwork and
Mark Borgerding for a portion of included FFT code (kissfft) — worth keeping
in mind if this project ever imports ft8_lib source files directly rather
than reimplementing against its published behavior, since kissfft carries
its own (BSD-style) license.

---

## 2. SDR++ (waterfall color palettes)

- **Project:** SDR++
- **Author:** Alexandre Rouma and contributors
- **Source:** https://github.com/AlexandreRouma/SDRPlusPlus
- **License:** GPL-3.0-only (core application); the repository's overall
  license set also includes MIT/WTFPL/Public-Domain-licensed components for
  specific files, but the application and its `res/colormaps/*.json`
  palette files fall under the project's GPL-3.0 license.
- **Used for:** reference/inspiration for waterfall color scheme naming and
  visual style (e.g. "classic", "classic_green", "electric", "gqrx",
  "greyscale", "turbo"-style gradients) on the DeepSDR 101 spectrum/waterfall
  display.


---


