![DirectX Logo](https://raw.githubusercontent.com/wiki/Microsoft/UVAtlas/Dx_logo.GIF)

# UVAtlas - isochart texture atlasing

http://go.microsoft.com/fwlink/?LinkID=512686

Copyright (c) Microsoft Corporation.

## July 9, 2025

This package contains UVAtlas, a shared source library for creating and packing an isochart texture atlas.

This code is designed to build with Visual Studio 2019 (16.11), Visual Studio 2022, clang for Windows v12 or later, or MinGW 12.2. Use of the Windows 10 May 2020 Update SDK ([19041](https://walbourn.github.io/windows-10-may-2020-update-sdk/)) or later is required for Visual Studio. It can also be built for Windows Subsystem for Linux using GCC 11 or later.

These components are designed to work without requiring any content from the legacy DirectX SDK. For details, see [Where is the DirectX SDK?](https://aka.ms/dxsdk).

## Directory Layout

* ``UVAtlas\``

  * ``Inc\``

    * Public Header File (in the DirectX C++ namespace):

      * UVtlas.h
        * UVAtlasCreate
        * UVAtlasPartition
        * UVAtlasPack
        * UVAtlasComputeIMTFromPerVertexSignal
        * UVAtlasComputeIMTFromSignal
        * UVAtlasComputeIMTFromTexture
        * UVAtlasComputeIMTFromPerTexelSignal
        * UVAtlasApplyRemap

  * ``geodesics\``, ``isochart\``

    * Library source files

* ``UVAtlasTool\``

  * Command line tool and sample for UVAtlas library

* ``build\``

  * Contains miscellaneous build files and scripts.

## Documentation

Documentation is available on the [GitHub wiki](https://github.com/Microsoft/UVAtlas/wiki).

## Notices

All content and source code for this package are subject to the terms of the [MIT License](https://github.com/microsoft/UVAtlas/blob/main/LICENSE).

For the latest version of UVAtlas, bug reports, etc. please visit the project site on [GitHub](https://github.com/microsoft/UVAtlas).

## Further reading

Zhou et al, "Iso-charts: Stretch-driven Mesh Parameterization using Spectral Analysis",
Eurographics Symposium on Geometry Processing (2004) [pdf](
http://research.microsoft.com/en-us/um/people/johnsny/papers/isochart.pdf)

Sander et al. "Signal-Specialized Parametrization" Europgraphics 2002 [pdf](http://research.microsoft.com/en-us/um/people/johnsny/papers/ssp.pdf)

## Release Notes

FOR SECURITY ADVISORIES, see [GitHub](https://github.com/microsoft/UVAtlas/security/advisories).

For a full change history, see [CHANGELOG.md](https://github.com/microsoft/UVAtlas/blob/main/CHANGELOG.md).

* Starting with the March 2025 release, Windows 7 and Windows 8.0 support has been retired.

* Starting with the December 2020 release, this library makes use of typed enum bitmask flags per the recommendation of the _C++ Standard_ section _17.5.2.1.3 Bitmask types_. This is consistent with Direct3D 12's use of the ``DEFINE_ENUM_FLAG_OPERATORS`` macro. This may have _breaking change_ impacts to client code:

  * You cannot pass the ``0`` literal as your option flags value. Instead you must make use of the appropriate default enum value: ``UVATLAS_DEFAULT`` or ``UVATLAS_IMT_DEFAULT``.

  * Use the enum type instead of ``DWORD`` if building up flags values locally with bitmask operations. For example, ``UVATLAS options = UVATLAS_DEFAULT; if (...) options |= UVATLAS_GEODESIC_FAST;``

* The UWP projects and the Win10 classic desktop project include configurations for the ARM64 platform. Building these requires installing the ARM64 toolset.

* For ARM64/AArch64 development, the VS 2022 compiler is strongly recommended over the VS 2019 toolset. The Windows SDK (26100 or later) is not compatible with VS 2019 for Win32 on ARM64 development. _Note that the ARM32/AArch32 platform is [deprecated](https://learn.microsoft.com/windows/arm/arm32-to-arm64)_.

* When using clang/LLVM for the ARM64/AArch64 platform, the Windows 11 SDK ([22000](https://walbourn.github.io/windows-sdk-for-windows-11/)) or later is required.

* As of the October 2024 release, the command-line tool also supports GNU-style long options using ``--``. All existing switches continue to function, but some of the `-` options are now deprecated per this table:

|Old switch|New switch|
|---|---|
|-sdkmesh|-ft sdkmesh<br />--file-type sdkmesh|
|-sdkmesh2|-ft sdkmesh2<br />--file-type sdkmesh2|
|-cmo|-ft cmo<br />--file-type cmo|
|-vbo|-ft vbo<br />--file-type vbo|
|-wf|-ft obj<br />--file-type obj|
|-flipu|--flip-u|
|-flipv|--flip-v|
|-flipz|--flip-z|

## Contributing

This project welcomes contributions and suggestions. Most contributions require you to agree to a Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us the rights to use your contribution. For details, visit https://cla.opensource.microsoft.com.

When you submit a pull request, a CLA bot will automatically determine whether you need to provide a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions provided by the bot. You will only need to do this once across all repos using our CLA.

Tests for new features should also be submitted as a PR to the [Test Suite](https://github.com/walbourn/uvatlastest/wiki) repository.

## Code of Conduct

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/). For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft trademarks or logos is subject to and must follow [Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general). Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship. Any use of third-party trademarks or logos are subject to those third-party's policies.

# Credits

The UVAtlas library is the work of Xin Huang and Chuck Walbourn, with contributions from Chris Messer, Steve Schroeder, Microsoft Research China, and Team Bungie.

Thanks to Andrew Farrier and Scott Matloff for their on-going help with code reviews.
