# UVAtlas - isochart texture atlasing

http://go.microsoft.com/fwlink/?LinkID=512686

Release available for download on [GitHub](https://github.com/microsoft/UVAtlas/releases)

## Release History

### March 24, 2022
* Update build switches for SDL recommendations
* CMake project updates and UWP platform CMakePresets
* Code cleaup for uvatlastool
* uvatlastool: Updated for March 2022 DirectXTex & DirectXMesh releases

### February 28, 2022
* Code and project review including fixing clang v13 warnings
* Added CMakePresets.json
* uvatlastool: Updated for February 2022 DirectXTex & DirectXMesh releases

### November 8, 2021
* VS 2022 support
* Fixed locale issue with WaveFront OBJ reading/writing
* Minor code and project review
* VS 2017 projects updated to require the Windows 10 SDK (19401)
* uvatlastool: Updated for November 2021 DirectXTex & DirectXMesh releases

### September 28, 2021
* Minor code and project cleanup

### June 9, 2021
* CMake build option to use Eigen3 library
* Code cleanup
* uvatlastool: Added ``-uv2`` switch to store second UV set in SDKMESH with isochart
* utatlastool: improved ``-flist`` switch to support wildcards and file exclusions
* uvatlastool: Updated for June 2021 DirectXTex & DirectXMesh releases

### April 6, 2021
* Minor code and project cleanup
* uvatlastool: Updated with descriptions for HRESULT failure codes
* uvatlastool: Updated for April 2021 DirectXTex & DirectXMesh releases

### January 9, 2021
* Windows Subsystem for Linux support
* Code review for improved conformance
* CMake updated to support package install

### December 1, 2020
* Converted to typed enum bitmask flags (see release notes for details on this potential *breaking change*)
* Added new partition options ``UVATLAS_LIMIT_MERGE_STRETCH`` and ``UVATLAS_LIMIT_FACE_STRETCH``
* Applied patch to fix (occasionally) bad gutter buffer calculation
* uvatlastool: Added ``-lms`` and ``-lfs`` switches
* Minor project cleanup

### November 11, 2020
* uvatlastool: Updated for November 2020 DirectXTex & DirectXMesh releases
* Minor project cleanup

### August 15, 2020
* Project updates
* uvatlastool: Updated for August 2020 DirectXTex & DirectXMesh releases
* uvatlastool: Added ``-fn``, ``-fuc``, and ``-fc`` switches to control vertex format

### July 2, 2020
* Minor warning fixes for VS 2019 (16.7)

### June 1, 2020
* uvatlastool: Updated for June 2020 DirectXTex & DirectXMesh releases
* CMake project updates
* Minor code cleanup

### May 10, 2020
* Minor code review
* uvatlastool: Updated with ``-l`` switch for case-sensitive file systems
* CMake updated for PCH usage with 3.16 or later

### February 24, 2020
* Added some simple OpenMP optimizations
* Code and project cleaup
* Retired VS 2015 projects
* uvatlastool: Updated to use NuGet instead of needing DirectXTex/Mesh side-by-side
* uvatlastool: now supports exporting to WaveFront Object (OBJ) files

### December 17, 2019
* Added VS 2019 UWP project
* Added ARM64 platform to VS 2019 Win32 desktop Win10 project
* Renamed ``UVAtlas_Windows10.vcxproj`` to ``_Windows10_2017.vcxproj``
* Added CMake project files
* Code cleanup

### April 26, 2019
* uvatlas command-line tool: Updated for latest DirectXMesh
* Added VS 2019 desktop projects
* Officially dropped Windows Vista support

### February 8, 2019
* uvatlastool: added ``-sdkmesh2`` switch for PBR materials

### November 16, 2018
* VS 2017 updated for Windows 10 October 2018 Update SDK (17763)
* ARM64 platform configurations added to UWP projects
* Minor code review

### August 17, 2018
* Updated for VS 2017 15.8
* Code cleanup

### July 3, 2018
* Code cleanup
* uvatlastool: added ``-flipu`` and ``-ib32`` switches

### May 31, 2018
* VS 2017 updated for Windows 10 April 2018 Update SDK (17134)

### May 14, 2018
* Updated for VS 2017 15.7 update warnings
* Code and project cleanup
* Retired VS 2013 projects

### April 23, 2018
* Code and project cleanup

### February 7, 2018
* Minor code update

### December 13, 2017
* Updated for VS 2017 15.5 update warnings

### November 1, 2017
* VS 2017 updated for Windows 10 Fall Creators Update SDK (16299)
* Removed ``UVAtlas_2017.vcxproj`` as redundant in favor of ``UVAtlas_2017_Win10.vcxproj``

### September 22, 2017
* Updated for VS 2017 15.3 update ``/permissive-`` changes
* uvatlastool: added ``-flist`` option

### July 28, 2017
* Code cleanup

### April 24, 2017
* VS 2017 project updates

### April 8, 2017
* VS 2017 updated for Windows Creators Update SDK (15063)
* Minor code cleanup

### January 31, 2017
* uvatlas command-line tool: Updated for latest DirectXMesh
* uvatlas command-line tool: optional OpenEXR support
* VS 2017 RC projects added
* Minor code cleanup

### September 14, 2016
* uvatlas command-line tool: added wildcard support for input filename and optional ``-r`` switch for recursive search
* uvatlas command-line tool ``-it`` switch now supports HDR (RGBE Radiance) texture files (requires DirectXTex September 2016 release to build)
* Code cleanup

### August 2, 2016
* Updated for VS 2015 Update 3 and Windows 10 SDK (14393)

### June 27, 2016
* Code cleanup

### April 26, 2016
* Retired VS 2012 projects and obsolete adapter code
* Minor code cleanup

### November 30, 2015
* uvatlas command-line tool updated with ``-flipv`` and ``-flipz`` switches; removed ``-fliptc``
* Updated for VS 2015 Update 1 and Windows 10 SDK (10586)

### October 30, 2015
* Minor code cleanup

### July 29, 2015
* Updated for VS 2015 and Windows 10 SDK RTM
* Retired VS 2010 and Windows 8.0 Store projects

### June 18, 2015
* Fixed bugs with handling of ``E_ABORT`` from user callback
* Added ESC to abort to uvatlas sample

### April 9, 2015
* Added projects for Windows apps Technical Preview
* Fixes for potential divide-by-zero cases
* Fix for memory allocation problem
* Added error detection for invalid partitioning
* uvatlastool: fix when importing from .vbo
* Minor code cleanup

### November 12, 2014
* Original release
