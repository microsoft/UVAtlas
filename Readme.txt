-----------------------------------
UVAtlas - isochart texture atlasing
-----------------------------------

Copyright (c) Microsoft Corporation. All rights reserved.

April 9, 2015

This package contains UVAtlas, a shared source library for creating and packing an
isochart texture atlas.

This code is designed to build with Visual Studio 2010, 2012, or 2013. It requires
the Windows 8.x SDK for functionality such as the DirectXMath library. Visual
Studio 2012 and 2013 already include the appropriate Windows SDK,
but Visual Studio 2010 users must install the standalone Windows 8.1 SDK. Details on
using the Windows 8.1 SDK with VS 2010 are described on the Visual C++ Team Blog:

<http://blogs.msdn.com/b/vcblog/archive/2012/11/23/using-the-windows-8-sdk-with-visual-studio-2010-configuring-multiple-projects.aspx>

These components are designed to work without requiring any content from the DirectX SDK. For details,
see "Where is the DirectX SDK?" <http://msdn.microsoft.com/en-us/library/ee663275.aspx>.

Inc\
    Public Header File (in the DirectX C++ namespace):

    UVtlas.h
    - UVAtlasCreate
    - UVAtlasPartition
    - UVAtlasPack
    - UVAtlasComputeIMTFromPerVertexSignal
    - UVAtlasComputeIMTFromSignal
    - UVAtlasComputeIMTFromTexture
    - UVAtlasComputeIMTFromPerTexelSignal
    - UVAtlasApplyRemap

geodesics\
isochart\
    Library source files

UVAtasTool\
    Command line tool and sample for UVAtlas library

    To build this tool, you need the DirectXTex (http://go.microsoft.com/fwlink/?LinkId=248926)
    and DirectXMesh (http://go.microsoft.com/fwlink/?LinkID=324981) libraries in the following
    directory structure:
        .\DirectXTex\DirectXTex
        .\DirectXMesh\DirectXMesh
        .\DirectXMesh\Utilities
        .\UVAtlas\UVAtlas
        .\UVAtlas\UVAtlasTool

All content and source code for this package are subject to the terms of the MIT License.
<http://opensource.org/licenses/MIT>.

For the latest version of UVAtlas, more detailed documentation, discussion forums, bug
reports and feature requests, please visit the Codeplex site.

http://go.microsoft.com/fwlink/?LinkID=512686

Further reading:

    Zhou et al, "Iso-charts: Stretch-driven Mesh Parameterization using Spectral Analysis",
    Eurographics Symposium on Geometry Processing (2004)
    http://research.microsoft.com/en-us/um/people/johnsny/papers/isochart.pdf

    Sander et al. "Signal-Specialized Parametrization" Europgraphics 2002
    http://research.microsoft.com/en-us/um/people/johnsny/papers/ssp.pdf


---------------
RELEASE HISTORY
---------------

April 9, 2015
    Added projects for Windows apps Technical Preview
    Fixes for potential divide-by-zero cases
    Fix for memory allocation problem
    Added error detection for invalid partitioning
    uvatlas: fix when importing from .vbo
    Minor code cleanup

November 12, 2014
    Original release
