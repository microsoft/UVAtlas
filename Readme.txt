-----------------------------------
UVAtlas - isochart texture atlasing
-----------------------------------

Copyright (c) Microsoft Corporation. All rights reserved.

August 17, 2018

This package contains UVAtlas, a shared source library for creating and packing an
isochart texture atlas.

This code is designed to build with Visual Studio 2015 Update 3 or Visual Studio 2017.
It is recommended that you make use of VS 2015 Update 3, Windows Tools 1.4.1, and the
Windows 10 Anniversary Update SDK (14393) or VS 2017 (15.8 update) with the
Windows 10 April 2018 Update SDK (17134).

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


-------------
RELEASE NOTES
-------------

* The VS 2017 projects make use of /permissive- for improved C++ standard conformance. Use of a Windows 10 SDK prior to
  the Fall Creators Update (16299) or an Xbox One XDK prior to June 2017 QFE 4 may result in failures due to problems
  with the system headers. You can work around these by disabling this switch in the project files which is found
  in the <ConformanceMode> elements.

* The VS 2017 projects require the 15.5 update or later. For UWP and Win32 classic desktop projects with the 15.5
  or 15.6 updates, you need to install the standalone Windows 10 SDK (17134) which is otherwise included in
  the 15.7/15.8 update. Older VS 2017 updates will fail to load the projects due to use of the <ConformanceMode> element.
  If using the 15.5 or 15.6 updates, you will see "warning D9002: ignoring unknown option '/Zc:__cplusplus'" because
  this switch isn't supported until 15.7. It is safe to ignore this warning, or you can edit the project files
  <AdditionalOptions> elements.


---------------
RELEASE HISTORY
---------------

August 17, 2018
    Updated for VS 2017 15.8
    Code cleanup

July 3, 2018
    Code cleanup
    uvatlas: added -flipu and -ib32 switches

May 31, 2018
    VS 2017 updated for Windows 10 April 2018 Update SDK (17134)

May 14, 2018
    Updated for VS 2017 15.7 update warnings
    Code and project cleanup
    Retired VS 2013 projects

April 23, 2018
    Code and project cleanup

February 7, 2018
    Minor code update

December 13, 2017
    Updated for VS 2017 15.5 update warnings

November 1, 2017
    VS 2017 updated for Windows 10 Fall Creators Update SDK (16299)
    Removed UVAtlas_2017.vcxproj as redundant in favor of UVAtlas_2017_Win10.vcxproj 

September 22, 2017
    Updated for VS 2017 15.3 update /permissive- changes
    uvatlas: added -flist option

July 28, 2017
    Code cleanup

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
