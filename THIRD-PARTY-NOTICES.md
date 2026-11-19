# Third-party notices

RedXe is copyright (c) 2026 RedSalamander, under the MIT license in [LICENSE.txt](LICENSE.txt). This file lists the
parts of the repository and of the portable package that carry other terms, and the third-party libraries RedXe
ships, with their license texts. A new dependency or asset extends it, with its full notice, before it is distributed.

## Files under other terms

The MIT License in [LICENSE.txt](LICENSE.txt) covers RedXe's own code, shaders, settings, documentation, and
artwork. These parts of the repository and of the portable package carry their own terms, which take precedence for
those files:

- `Plugins/5H4D3R5/Shaders/`: the twelve Shadertoy ports named in `Plugins/5H4D3R5/LICENSES.md` are adaptations of
  their authors' works under the Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License
  (CC BY-NC-SA 3.0). Each file keeps its author's header and license text; attribution is required, commercial use
  is not permitted, and adaptations must be shared alike. `CosmicOrb.hlsl`, the Sky Atmosphere scene, and the
  framework HLSL are RedXe's and are MIT. The Sky Atmosphere technique is Sébastien Hillaire's, published by Epic
  Games under the MIT License (notice in `Plugins/5H4D3R5/LICENSES.md`).
- `Plugins/Weather/Fonts/weathericons-regular-webfont.ttf` (shipped as `Plugins\weathericons-regular-webfont.ttf`):
  Weather Icons, Copyright (c) 2015 Erik Flowers, original icon designs by Lukas Bischoff, SIL Open Font License 1.1.
  The full license is `Plugins/Weather/Fonts/OFL.txt`, shipped beside the font as `Plugins\OFL.txt`.
- The Microsoft Visual C++ runtime DLLs bundled in the portable package are redistributed under the Microsoft
  Visual Studio distributable-code terms. Windows SDK and system libraries are supplied under Microsoft's terms.

## Third-Party Licenses

RedXe uses the following third-party libraries. Their license terms are reproduced below.

-------------------------------------------------------------------------------

### DxUi

- Source: https://github.com/RedSalamanders/DxUi (exact commit pinned in `Dependencies/DxUi.lock.json`)
- License: MIT
- Copyright (c) 2026 RedSalamander

The MIT License in [LICENSE.txt](LICENSE.txt) applies with that copyright notice. The DxUi revision RedXe pins compiles
in WIL (below); toml++ 3.4.0, Mark Gillard, MIT; a math symbol table derived from KaTeX 0.16.11, Khan Academy and
contributors, MIT; rapidyaml 0.16.0, Joao Paulo Magalhaes, MIT, with its bundled debugbreak (Scott Tsai, BSD 2-Clause)
and fast_float (MIT) notices; tables generated from Unicode 17.0.0, copyright Unicode, Inc., Unicode License V3; and an
HTML named-character table from the WHATWG snapshot of 18 October 2026, copyright WHATWG (Apple, Google, Mozilla,
Microsoft), BSD 3-Clause for code and CC BY 4.0. Their full texts are in DxUi at that commit; a package built from an
SDK carries them as `DxUi-licenses\`.

-------------------------------------------------------------------------------

### Windows Implementation Libraries (WIL)

- Source: https://github.com/microsoft/wil (vcpkg port `wil`)
- License: MIT

MIT License

Copyright (c) Microsoft Corporation. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE

-------------------------------------------------------------------------------

### yyjson

- Source: https://github.com/ibireme/yyjson (vcpkg port `yyjson`)
- License: MIT

MIT License

Copyright (c) 2020 YaoYuan <ibireme@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-------------------------------------------------------------------------------

### curl / libcurl

- Source: https://curl.se (vcpkg port `curl`, shipped as `Plugins\libcurl.dll`)
- License: curl license

COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2026, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not
be used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization of the copyright holder.


inet_ntop.c and inet_pton.c Notice:

/*
 * Copyright (C) 1996-2022  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM
 * DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 * INTERNET SOFTWARE CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * SPDX-License-Identifier: ISC
 */



-------------------------------------------------------------------------------

### zlib

- Source: https://zlib.net (vcpkg port `zlib`, shipped as `Plugins\z.dll`, a libcurl dependency)
- License: zlib

 (C) 1995-2026 Jean-loup Gailly and Mark Adler

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu
