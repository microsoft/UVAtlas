![DirectX Logo](https://github.com/Microsoft/UVAtlas/wiki/Dx_logo.GIF)

# UVAtlas - isochart texture atlasing

http://go.microsoft.com/fwlink/?LinkID=512686

Copyright (c) Microsoft Corporation. All rights reserved.

**June 1, 2020**

This package contains UVAtlas, a shared source library for creating and packing an isochart texture atlas.

This code is designed to build with Visual Studio 2017 ([15.9](https://walbourn.github.io/vs-2017-15-9-update/)), Visual Studio 2019, or clang for Windows v9 or later. It is recommended that you make use of the Windows 10 May 2020 Update SDK ([19041](https://walbourn.github.io/windows-10-may-2020-update-sdk/)).

These components are designed to work without requiring any content from the legacy DirectX SDK. For details, see [Where is the DirectX SDK?](https://aka.ms/dxsdk).

## Directory Layout

* ``Inc\``

  + Public Header File (in the DirectX C++ namespace):

    * UVtlas.h
      - UVAtlasCreate
      - UVAtlasPartition
      - UVAtlasPack
      - UVAtlasComputeIMTFromPerVertexSignal
      - UVAtlasComputeIMTFromSignal
      - UVAtlasComputeIMTFromTexture
      - UVAtlasComputeIMTFromPerTexelSignal
      - UVAtlasApplyRemap

* ``geodesics\``, ``isochart\``

  + Library source files

* ``UVAtasTool\``

  + Command line tool and sample for UVAtlas library

## Documentation

Documentation is available on the [GitHub wiki](https://github.com/Microsoft/UVAtlas/wiki).

## Notices

All content and source code for this package are subject to the terms of the [MIT License](http://opensource.org/licenses/MIT).

For the latest version of UVAtlas, bug reports, etc. please visit the project site on [GitHub](https://github.com/microsoft/UVAtlas).

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/). For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Further reading

Zhou et al, "Iso-charts: Stretch-driven Mesh Parameterization using Spectral Analysis",
Eurographics Symposium on Geometry Processing (2004) [pdf](
http://research.microsoft.com/en-us/um/people/johnsny/papers/isochart.pdf)

Sander et al. "Signal-Specialized Parametrization" Europgraphics 2002 [pdf](http://research.microsoft.com/en-us/um/people/johnsny/papers/ssp.pdf)

## Release Notes

* The UWP projects and the VS 2019 Win10 classic desktop project include configurations for the ARM64 platform. These require VS 2017 (15.9 update) or VS 2019 to build, with the ARM64 toolset installed.
