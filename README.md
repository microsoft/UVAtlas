![DirectX Logo](https://github.com/Microsoft/UVAtlas/wiki/Dx_logo.GIF)

# UVAtlas - isochart texture atlasing

http://go.microsoft.com/fwlink/?LinkID=512686

Copyright (c) Microsoft Corporation. All rights reserved.

**February 24, 2020**

This package contains UVAtlas, a shared source library for creating and packing an isochart texture atlas.

This code is designed to build with Visual Studio 2017 ([15.9](https://walbourn.github.io/vs-2017-15-9-update/)), Visual Studio 2019, or clang for Windows v9. It is recommended that you make use of the Windows 10 May 2019 Update SDK ([18362](https://walbourn.github.io/windows-10-may-2019-update/)).

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

* The VS 2017/2019 projects make use of ``/permissive-`` for improved C++ standard conformance. Use of a Windows 10 SDK prior to the Fall Creators Update (16299) or an Xbox One XDK prior to June 2017 QFE 4 may result in failures due to problems with the system headers. You can work around these by disabling this switch in the project files which is found in the ``<ConformanceMode>`` elements, or in some cases adding ``/Zc:twoPhase-`` to the ``<AdditionalOptions>`` elements.

* The VS 2017 projects require the 15.5 update or later. For UWP and Win32 classic desktop projects with the 15.5 - 15.7 updates, you need to install the standalone Windows 10 SDK (17763) which is otherwise included in the 15.8.6 or later update. Older VS 2017 updates will fail to load the projects due to use of the <ConformanceMode> element. If using the 15.5 or 15.6 updates, you will see ``warning D9002: ignoring unknown option '/Zc:__cplusplus'`` because this switch isn't supported until 15.7. It is safe to ignore this warning, or you can edit the project files ``<AdditionalOptions>`` elements.

* The VS 2019 projects use a ``<WindowsTargetPlatformVersion>`` of ``10.0`` which indicates to use the latest installed version. This should be Windows 10 SDK (17763) or later.

* The UWP projects and the VS 2019 Win10 classic desktop project include configurations for the ARM64 platform. These require VS 2017 (15.9 update) or VS 2019 to build, with the ARM64 toolset installed.
