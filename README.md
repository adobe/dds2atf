dds2atf
=======

A tool for converting DDS files into ATF files suitable for use with the Flash Stage3D API. The source for the Adobe written part is available under the MIT license, but code in the 3rdparty directory may have other licenses or conditions attached.

Compiled binaries are available in the bin directory if you don't want to compile the source yourself.


Usage
=====

<pre>
dds2atf [-4|-2|-0] [-q <0-180>] [-f <0-15>] -i input.dds -o output.atf

   -n  Embed a specific range of texture levels (main texture + mip map) for texture streaming. 
       The range is defined as <start>,<end>. 0 is the main texture, mip map starts with 1.

Options for non-block compressed texture:
   -4  Use 4:4:4 colorspace (default)
   -2  Use 4:2:2 colorspace
   -0  Use 4:2:0 colorspace

   -q  quantization level. 0 == lossless, higher values create compression artifacts.
   -f  trim flex bits. 0 == lossless, higher values create compression artifacts.
</pre>