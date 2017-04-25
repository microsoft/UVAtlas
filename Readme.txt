-----------------------------------
UVAtlas - isochart texture atlasing
-----------------------------------

Copyright (c) Microsoft Corporation. All rights reserved.

April 24, 2017

This package contains UVAtlas, a shared source library for creating and packing an
isochart texture atlas.

The source is written for Visual Studio 2013 or 2015. It is recommended that you
make use of VS 2013 Update 5 or VS 2015 Update 3 and Windows 7 Service Pack 1 or later.

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

Documentation is available at <https://github.com/Microsoft/UVAtlas/wiki>.

For the latest version of UVAtlas, bug reports, etc. please visit the project site.

http://go.microsoft.com/fwlink/?LinkID=512686

This project has adopted the Microsoft Open Source Code of Conduct. For more information see the
Code of Conduct FAQ or contact opencode@microsoft.com with any additional questions or comments.

https://opensource.microsoft.com/codeofconduct/

Further reading:

    Zhou et al, "Iso-charts: Stretch-driven Mesh Parameterization using Spectral Analysis",
    Eurographics Symposium on Geometry Processing (2004)
    http://research.microsoft.com/en-us/um/people/johnsny/papers/isochart.pdf

    Sander et al. "Signal-Specialized Parametrization" Europgraphics 2002
    http://research.microsoft.com/en-us/um/people/johnsny/papers/ssp.pdf


---------------
RELEASE HISTORY
---------------

April 24, 2017
    VS 2017 project updates

April 8, 2017
    VS 2017 updated for Windows Creators Update SDK (15063)
    Minor code cleanup

January 31, 2017
    uvatlas command-line tool: Updated for latest DirectXMesh
    uvatlas command-line tool: optional OpenEXR support
    VS 2017 RC projects added
    Minor code cleanup

September 14, 2016
    uvatlas command-line tool: added wildcard support for input filename and optional -r switch for recursive search
    uvatlas command-line tool -it switch now supports HDR (RGBE Radiance) texture files (requires DirectXTex September 2016 release to build)
    Code cleanup

August 2, 2016
    Updated for VS 2015 Update 3 and Windows 10 SDK (14393)

June 27, 2016
    Code cleanup

April 26, 2016
    Retired VS 2012 projects and obsolete adapter code
    Minor code cleanup

November 30, 2015
    uvatlas command-line tool updated with -flipv and -flipz switches; removed -fliptc
    Updated for VS 2015 Update 1 and Windows 10 SDK (10586)

October 30, 2015
    Minor code cleanup

July 29, 2015
    Updated for VS 2015 and Windows 10 SDK RTM
    Retired VS 2010 and Windows 8.0 Store projects

June 18, 2015
    Fixed bugs with handling of E_ABORT from user callback
    Added ESC to abort to uvatlas sample

April 9, 2015
    Added projects for Windows apps Technical Preview
    Fixes for potential divide-by-zero cases
    Fix for memory allocation problem
    Added error detection for invalid partitioning
    uvatlas: fix when importing from .vbo
    Minor code cleanup

November 12, 2014
    Original release
