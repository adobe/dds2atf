/*
Copyright (c) 2012 Adobe Systems Incorporated

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <math.h>

#ifdef _MSC_VER
#include <windows.h>
#endif //#ifdef _MSC_VER

#ifndef _MSC_VER
#include <stdint.h>
#endif //#ifndef _MSC_VER

#include "3rdparty/jpegxr/jpegxr.h"
#include "3rdparty/jpegxr/jxr_priv.h"
#include "3rdparty/lzma/LzmaLib.h"
#include "atf.h"

using namespace std;

/* DDS loader written by Jon Watte 2002 */
/* Permission granted to use freely, as long as Jon Watte */
/* is held harmless for all possible damages resulting from */
/* your use or failure to use this code. */
/* No warranty is expressed or implied. Use at your own risk, */
/* or not at all. */

//  little-endian, of course
#define DDS_MAGIC 0x20534444

//  DDS_header.dwFlags
#define DDSD_CAPS                   0x00000001 
#define DDSD_HEIGHT                 0x00000002 
#define DDSD_WIDTH                  0x00000004 
#define DDSD_PITCH                  0x00000008 
#define DDSD_PIXELFORMAT            0x00001000 
#define DDSD_MIPMAPCOUNT            0x00020000 
#define DDSD_LINEARSIZE             0x00080000 
#define DDSD_DEPTH                  0x00800000 

//  DDS_header.sPixelFormat.dwFlags
#define DDPF_ALPHAPIXELS            0x00000001 
#define DDPF_FOURCC                 0x00000004 
#define DDPF_INDEXED                0x00000020 
#define DDPF_RGB                    0x00000040 

//  DDS_header.sCaps.dwCaps1
#define DDSCAPS_COMPLEX             0x00000008 
#define DDSCAPS_TEXTURE             0x00001000 
#define DDSCAPS_MIPMAP              0x00400000 

//  DDS_header.sCaps.dwCaps2
#define DDSCAPS2_CUBEMAP            0x00000200 
#define DDSCAPS2_CUBEMAP_POSITIVEX  0x00000400 
#define DDSCAPS2_CUBEMAP_NEGATIVEX  0x00000800 
#define DDSCAPS2_CUBEMAP_POSITIVEY  0x00001000 
#define DDSCAPS2_CUBEMAP_NEGATIVEY  0x00002000 
#define DDSCAPS2_CUBEMAP_POSITIVEZ  0x00004000 
#define DDSCAPS2_CUBEMAP_NEGATIVEZ  0x00008000 
#define DDSCAPS2_VOLUME             0x00200000 

#define D3DFMT_DXT1     '1TXD'    //  DXT1 compression texture format 
#define D3DFMT_DXT2     '2TXD'    //  DXT2 compression texture format 
#define D3DFMT_DXT3     '3TXD'    //  DXT3 compression texture format 
#define D3DFMT_DXT4     '4TXD'    //  DXT4 compression texture format 
#define D3DFMT_DXT5     '5TXD'    //  DXT5 compression texture format 
#define D3DFMT_ATI2     '2ITA'    //  ATI2 compression texture format 
#define D3DFMT_ATI1     '1ITA'    //  ATI1 compression texture format 
#define D3DFMT_BC4U     'U4CB'    //  BC4U compression texture format 
#define D3DFMT_BC4S     'S4CB'    //  BC4S compression texture format 
#define D3DFMT_BC5U     'U5CB'    //  BC4U compression texture format 
#define D3DFMT_BC5S     'S5CB'    //  BC4S compression texture format 

#define PF_IS_BC5S(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_FOURCC) && \
   (pf.sPixelFormat.dwFourCC == D3DFMT_BC5S))

#define PF_IS_BC5U(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_FOURCC) && \
   (pf.sPixelFormat.dwFourCC == D3DFMT_BC5U))

#define PF_IS_BC4S(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_FOURCC) && \
   (pf.sPixelFormat.dwFourCC == D3DFMT_BC4S))

#define PF_IS_BC4U(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_FOURCC) && \
   (pf.sPixelFormat.dwFourCC == D3DFMT_BC4U))

#define PF_IS_ATI1(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_FOURCC) && \
   (pf.sPixelFormat.dwFourCC == D3DFMT_ATI1))

#define PF_IS_ATI2(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_FOURCC) && \
   (pf.sPixelFormat.dwFourCC == D3DFMT_ATI2))

#define PF_IS_DXT1(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_FOURCC) && \
   (pf.sPixelFormat.dwFourCC == D3DFMT_DXT1))

#define PF_IS_DXT3(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_FOURCC) && \
   (pf.sPixelFormat.dwFourCC == D3DFMT_DXT3))

#define PF_IS_DXT5(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_FOURCC) && \
   (pf.sPixelFormat.dwFourCC == D3DFMT_DXT5))

#define PF_IS_BGRA8(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_RGB) && \
   (pf.sPixelFormat.dwFlags & DDPF_ALPHAPIXELS) && \
   (pf.sPixelFormat.dwRGBBitCount == 32) && \
   (pf.sPixelFormat.dwRBitMask == 0xff0000) && \
   (pf.sPixelFormat.dwGBitMask == 0xff00) && \
   (pf.sPixelFormat.dwBBitMask == 0xff) && \
   (pf.sPixelFormat.dwAlphaBitMask == 0xff000000U))

#define PF_IS_BGR8(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_RGB) && \
  !(pf.sPixelFormat.dwFlags & DDPF_ALPHAPIXELS) && \
   (pf.sPixelFormat.dwRGBBitCount == 24) && \
   (pf.sPixelFormat.dwRBitMask == 0xff0000) && \
   (pf.sPixelFormat.dwGBitMask == 0xff00) && \
   (pf.sPixelFormat.dwBBitMask == 0xff))

#define PF_IS_BGRX8(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_RGB) && \
  !(pf.sPixelFormat.dwFlags & DDPF_ALPHAPIXELS) && \
   (pf.sPixelFormat.dwRGBBitCount == 32) && \
   (pf.sPixelFormat.dwRBitMask == 0xff0000) && \
   (pf.sPixelFormat.dwGBitMask == 0xff00) && \
   (pf.sPixelFormat.dwBBitMask == 0xff))

#define PF_IS_BGR5A1(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_RGB) && \
   (pf.sPixelFormat.dwFlags & DDPF_ALPHAPIXELS) && \
   (pf.sPixelFormat.dwRGBBitCount == 16) && \
   (pf.sPixelFormat.dwRBitMask == 0x00007c00) && \
   (pf.sPixelFormat.dwGBitMask == 0x000003e0) && \
   (pf.sPixelFormat.dwBBitMask == 0x0000001f) && \
   (pf.sPixelFormat.dwAlphaBitMask == 0x00008000))

#define PF_IS_BGR565(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_RGB) && \
  !(pf.sPixelFormat.dwFlags & DDPF_ALPHAPIXELS) && \
   (pf.sPixelFormat.dwRGBBitCount == 16) && \
   (pf.sPixelFormat.dwRBitMask == 0x0000f800) && \
   (pf.sPixelFormat.dwGBitMask == 0x000007e0) && \
   (pf.sPixelFormat.dwBBitMask == 0x0000001f))

#define PF_IS_INDEX8(pf) \
  ((pf.sPixelFormat.dwFlags & DDPF_INDEXED) && \
   (pf.sPixelFormat.dwRGBBitCount == 8))

#define PF_IS_SINGLECHANNEL(pf) \
  (!(pf.sPixelFormat.dwFlags & DDPF_INDEXED) && \
   (pf.sPixelFormat.dwRGBBitCount == 8) && \
   ((pf.sPixelFormat.dwRBitMask == 0x000000FF) || \
   (pf.sPixelFormat.dwGBitMask == 0x0000FF00) || \
   (pf.sPixelFormat.dwBBitMask == 0x00FF0000) || \
   (pf.sPixelFormat.dwAlphaBitMask == 0xFF000000)))


union DDS_header {
  struct {
    unsigned int    dwMagic;
    unsigned int    dwSize;
    unsigned int    dwFlags;
    unsigned int    dwHeight;
    unsigned int    dwWidth;
    unsigned int    dwPitchOrLinearSize;
    unsigned int    dwDepth;
    unsigned int    dwMipMapCount;
    unsigned int    dwReserved1[ 11 ];

    //  DDPIXELFORMAT
    struct {
      unsigned int    dwSize;
      unsigned int    dwFlags;
      unsigned int    dwFourCC;
      unsigned int    dwRGBBitCount;
      unsigned int    dwRBitMask;
      unsigned int    dwGBitMask;
      unsigned int    dwBBitMask;
      unsigned int    dwAlphaBitMask;
    }               sPixelFormat;

    //  DDCAPS2
    struct {
      unsigned int    dwCaps1;
      unsigned int    dwCaps2;
      unsigned int    dwDDSX;
      unsigned int    dwReserved;
    }               sCaps;
    unsigned int    dwReserved2;
  };
  char data[ 128 ];
};


using namespace std;

extern int32_t	gCompressedFormats;
extern bool		gEncodeRawJXR;
extern bool		gCheckForAlphaValue;
extern bool		gSilent;
extern bool		gTrimFlexBitsDefault ;
extern int32_t	gTrimFlexBits;
extern bool		gJxrFormatDefault;
extern jxr_color_fmt_t gJxrFormat;
extern bool		gJxrQualityDefault;
extern int32_t	gJxrQuality;
extern int32_t  gEmbedRangeStart;
extern int32_t  gEmbedRangeEnd;

extern size_t	infilesize;
extern size_t	outfilesize;
extern size_t	outlzmasize;
extern size_t	texturew;
extern size_t	textureh;
extern size_t	texturecomp;

extern bool convert(istream &ifile_etc1, istream &ifile_pvrtc, istream &ifile_dxt1, istream &ifile_raw, ostream &ofile);	
extern bool convert_with_alpha(istream &ifile_etc1, istream &ifile_pvrtc, istream &ifile_dxt5, ostream &ofile);

void print_usage()
{
	cout << "\ndds2atf V0.4 Copyright 2010-2012 Adobe Systems Inc. All rights reserved.\n\n";
	cout << "\nUsage: dds2atf [-4|-2|-0] [-q <0-180>] [-f <0-15>] -i input.dds -o output.atf\n\n";
	cout << "   -n  Embed a specific range of texture levels (main texture + mip map) for texture streaming. The range is defined as <start>,<end>. 0 is the main texture, mip map starts with 1.\n\n";
    cout << "Options for non-block compressed texture:\n";
	cout << "   -4  Use 4:4:4 colorspace (default)\n";
	cout << "   -2  Use 4:2:2 colorspace\n";
	cout << "   -0  Use 4:2:0 colorspace\n\n";
	cout << "   -q  quantization level. 0 == lossless, higher values create compression artifacts.\n";
	cout << "   -f  trim flex bits. 0 == lossless, higher values create compression artifacts.\n\n";
}

const char *ofilename = 0;

static ifstream ifile;
static ofstream ofile;
static stringstream *tfile;
static stringstream *dfile;

static bool set_dxt1_header(uint8_t *dst, int width, int height, int count, bool cubemap, size_t textureLen)
{
	PVR_HEADER *header = (PVR_HEADER *)dst;
	
	header->dwHeaderSize = sizeof(PVR_HEADER);
	header->dwWidth      = width;
	header->dwHeight     = height;
	header->dwMipMapCount= count;
	header->dwTextureDataSize = textureLen;
	header->dwpfFlags	 = header->dwMipMapCount ? PVRTEX_MIPMAP : 0;
	header->dwPVR[0]	 = 'P';
	header->dwPVR[1]	 = 'V';
	header->dwPVR[2]	 = 'R';
	header->dwPVR[3]	 = '!';
	header->dwNumSurfs	 = 1;
	
	header->dwRBitMask = 0xFFFFFFFF;
	header->dwGBitMask = 0xFFFFFFFF;
	header->dwBBitMask = 0xFFFFFFFF;
	header->dwpfFlags |= PVR_D3D_DXT1;
	if ( cubemap ) {
		header->dwpfFlags |= PVRTEX_CUBEMAP | PVRTEX_DDSCUBEMAPORDER;
	}
	return true;
}

static bool set_dxt5_header(uint8_t *dst, int width, int height, int count, bool cubemap, size_t textureLen)
{
	PVR_HEADER *header = (PVR_HEADER *)dst;
	
	header->dwHeaderSize = sizeof(PVR_HEADER);
	header->dwWidth      = width;
	header->dwHeight     = height;
	header->dwMipMapCount= count;
	header->dwTextureDataSize = textureLen;
	header->dwpfFlags	 = header->dwMipMapCount ? PVRTEX_MIPMAP : 0;
	header->dwPVR[0]	 = 'P';
	header->dwPVR[1]	 = 'V';
	header->dwPVR[2]	 = 'R';
	header->dwPVR[3]	 = '!';
	header->dwNumSurfs	 = 1;
	
	header->dwRBitMask = 0xFFFFFFFF;
	header->dwGBitMask = 0xFFFFFFFF;
	header->dwBBitMask = 0xFFFFFFFF;
	header->dwpfFlags |= PVR_D3D_DXT5;
	if ( cubemap ) {
		header->dwpfFlags |= PVRTEX_CUBEMAP | PVRTEX_DDSCUBEMAPORDER;
	}
	return true;
}

static bool set_bgr_header(uint8_t *dst, int width, int height, int count, bool cubemap, size_t textureLen)
{
	PVR_HEADER *header = (PVR_HEADER *)dst;
	
	header->dwHeaderSize = sizeof(PVR_HEADER);
	header->dwWidth      = width;
	header->dwHeight     = height;
	header->dwMipMapCount= count;
	header->dwTextureDataSize = textureLen;
	header->dwpfFlags	 = header->dwMipMapCount ? PVRTEX_MIPMAP : 0;
	header->dwPVR[0]	 = 'P';
	header->dwPVR[1]	 = 'V';
	header->dwPVR[2]	 = 'R';
	header->dwPVR[3]	 = '!';
	header->dwNumSurfs	 = 1;
	
    header->dwRBitMask = 0xFF;
    header->dwGBitMask = 0xFF;
    header->dwBBitMask = 0xFF;
    header->dwBitCount = 24;
    header->dwpfFlags |= PVR_OGL_RGB_888;
	if ( cubemap ) {
		header->dwpfFlags |= PVRTEX_CUBEMAP | PVRTEX_DDSCUBEMAPORDER;
	}
	return true;
}

static bool set_bgra_header(uint8_t *dst, int width, int height, int count, bool cubemap, size_t textureLen)
{
	PVR_HEADER *header = (PVR_HEADER *)dst;
	
	header->dwHeaderSize = sizeof(PVR_HEADER);
	header->dwWidth      = width;
	header->dwHeight     = height;
	header->dwMipMapCount= count;
	header->dwTextureDataSize = textureLen;
	header->dwpfFlags	 = header->dwMipMapCount ? PVRTEX_MIPMAP : 0;
	header->dwPVR[0]	 = 'P';
	header->dwPVR[1]	 = 'V';
	header->dwPVR[2]	 = 'R';
	header->dwPVR[3]	 = '!';
	header->dwNumSurfs	 = 1;
	
	header->dwRBitMask = 0xFF;
	header->dwGBitMask = 0xFF;
	header->dwBBitMask = 0xFF;
	header->dwAlphaBitMask = 0xFF;
	header->dwBitCount = 32;
	header->dwpfFlags |= PVR_OGL_RGBA_8888;
	if ( cubemap ) {
		header->dwpfFlags |= PVRTEX_CUBEMAP | PVRTEX_DDSCUBEMAPORDER;
	}
	return true;
}

inline int32_t log2(int32_t x) {
    return	(((((x) & 0xAAAAAAAA)?1:0)     )|
             ((((x) & 0xCCCCCCCC)?1:0) << 1)|
             ((((x) & 0xF0F0F0F0)?1:0) << 2)|
             ((((x) & 0xFF00FF00)?1:0) << 3)|
             ((((x) & 0xFFFF0000)?1:0) << 4));
}

static int32_t calcActualMipLevels(DDS_header *dds, int32_t size, int32_t &actualFileSize, int32_t &actualTextureSize)
{
    int32_t actual = 0;
    int32_t w = dds->dwWidth;
    int32_t h = dds->dwHeight;
    int32_t texLen = 0;
    int32_t wc = 0;
    int32_t hc = 0;
    int32_t fc = 0;

	for (int32_t c=0; c<=dds->dwMipMapCount; c++) {
        if ( PF_IS_DXT1((*dds)) ) {
			int32_t w4 = max(1,(w/4));
			int32_t h4 = max(1,(h/4));
            texLen += w4*h4*sizeof(uint32_t)*2*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        } else if ( PF_IS_DXT5((*dds)) ) {
			int32_t w4 = max(1,(w/4));
			int32_t h4 = max(1,(h/4));
            texLen += w4*h4*sizeof(uint32_t)*4*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        } else if ( PF_IS_BGRA8((*dds)) || PF_IS_BGRX8((*dds)) ) {
			texLen += w*h*4*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        } else if ( PF_IS_BGR8((*dds)) ) {
            texLen += w*h*3*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        } else if ( PF_IS_SINGLECHANNEL((*dds)) ) {
            texLen += w*h*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        }
        if ( texLen > size ) {
            actual = c-1;
            goto done;
        }
        w /= 2;
        h /= 2;
	}
    wc = log2((int32_t)dds->dwWidth);
    hc = log2((int32_t)dds->dwHeight);
    fc = dds->dwMipMapCount;
    actual = min(max(wc,hc),fc);
done:
    w = dds->dwWidth;
    h = dds->dwHeight;
    texLen = 0;
    int32_t fileLen = 0;
	for (int32_t c=0; c<=actual; c++) {
        if ( PF_IS_DXT1((*dds)) ) {
			int32_t w4 = max(1,(w/4));
			int32_t h4 = max(1,(h/4));
            texLen += w4*h4*sizeof(uint32_t)*2;
            fileLen += w4*h4*sizeof(uint32_t)*2*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        } else if ( PF_IS_DXT5((*dds)) ) {
			int32_t w4 = max(1,(w/4));
			int32_t h4 = max(1,(h/4));
            texLen += w4*h4*sizeof(uint32_t)*4;
            fileLen += w4*h4*sizeof(uint32_t)*4*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        } else if ( PF_IS_BGRA8((*dds)) || PF_IS_BGRX8((*dds)) ) {
			texLen += w*h*4;
			fileLen += w*h*4*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        } else if ( PF_IS_BGR8((*dds)) ) {
            texLen += w*h*3;
            fileLen += w*h*3*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        } else if ( PF_IS_SINGLECHANNEL((*dds)) ) {
            texLen += w*h;
            fileLen += w*h*((dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?6:1);
        }
        w /= 2;
        h /= 2;
	}
    actualFileSize = fileLen;
    actualTextureSize = texLen;
    return actual;
}

int main(int argc, char *argv[]) {

    gJxrFormatDefault = false;
	gJxrFormat = JXR_YUV444;
    
	if ( argc > 1) {
		for (int32_t c = 1; c < argc; c++) {
			if (argv[c][0] == '-') {
                if (argv[c][1] == 'n') {
					std::istringstream s(argv[c+1]);
                    char dummy;
                    s >> gEmbedRangeStart >> dummy >> gEmbedRangeEnd;
				} else if (argv[c][1] == 's') {
					gSilent = true;
				} else if (argv[c][1] == '4') {
					gJxrFormat = JXR_YUV444;
					gJxrFormatDefault = false;
				} else if (argv[c][1] == '2') {
					gJxrFormat = JXR_YUV422;
					gJxrFormatDefault = false;
				} else if (argv[c][1] == '0') {
					gJxrFormat = JXR_YUV420;
					gJxrFormatDefault = false;
				} else if (argv[c][1] == 'f') {
					std::istringstream s(argv[c+1]);
					s >> gTrimFlexBits;
					gTrimFlexBits = max(0,min(15,gTrimFlexBits));
					gTrimFlexBitsDefault = false;
				} else if (argv[c][1] == 'q') {
					int32_t quality = 0;
					std::istringstream s(argv[c+1]);
					s >> gJxrQuality;
					gJxrQuality = max(0,min(100,gJxrQuality));
					gJxrQualityDefault = false;
				} else if (argv[c][1] == 'i') {
					ifile.open(argv[c+1],ios::in|ios::binary);
	                if ( !ifile.is_open() ) {
		                cerr << "Could not open input file. '";
		                cerr << argv[c+1];
		                cerr << "'\n\n";
		                return -1;
	                }
				} else if (argv[c][1] == 'o') {
					if ( argc < c+1 ) {
						cerr << "Missing output file name.\n\n";
						return -1;
					}
                    ofilename = argv[c+1];
				}
			}
		}

        if ( !ifile.is_open() ) {
            cerr << "No input file provided.\n";
            goto printusage;
        }

        if ( !ofilename || strlen(ofilename) == 0 ) {
            cerr << "No output file provided.\n";
            goto printusage;
        }

	    ifile.seekg(0,ios_base::end);
	    size_t filesize = ifile.tellg();
	    ifile.seekg(0,ios_base::beg);

        uint8_t *src = new uint8_t [filesize];
        ifile.read((char *)src,filesize);

        DDS_header *dds = (DDS_header *)src;
        if ( dds->dwMagic != DDS_MAGIC ) {
			cerr << "Input file not a DDS file.\n";
			return -1;
        }

        int32_t actualTextureSize = 0;
        int32_t actualFileSize = 0;
        int32_t actualMipLevels = calcActualMipLevels(dds,filesize-sizeof(DDS_header),actualFileSize,actualTextureSize);
        int32_t strayBytes = (filesize-sizeof(DDS_header)) - actualFileSize;

        PVR_HEADER pvr;
        if ( PF_IS_DXT1((*dds)) ) {
            set_dxt1_header((uint8_t*)&pvr,dds->dwWidth,dds->dwHeight,actualMipLevels,(dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?true:false,actualTextureSize);
            gEncodeRawJXR = false;
        } else if ( PF_IS_DXT5((*dds)) ) {
            set_dxt5_header((uint8_t*)&pvr,dds->dwWidth,dds->dwHeight,actualMipLevels,(dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?true:false,actualTextureSize);
            gEncodeRawJXR = false;
        } else if ( PF_IS_BGRA8((*dds)) ) {
            set_bgra_header((uint8_t*)&pvr,dds->dwWidth,dds->dwHeight,actualMipLevels,(dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?true:false,actualTextureSize);
            gEncodeRawJXR = true;
        } else if ( PF_IS_BGR8((*dds)) || PF_IS_SINGLECHANNEL((*dds)) || PF_IS_BGRX8((*dds))) {
            set_bgr_header((uint8_t*)&pvr,dds->dwWidth,dds->dwHeight,actualMipLevels,(dds->sCaps.dwCaps2&DDSCAPS2_CUBEMAP)?true:false,actualTextureSize);
            gEncodeRawJXR = true;
        } else {
            if ( PF_IS_ATI1((*dds)) || PF_IS_BC4U((*dds)) || PF_IS_BC4S((*dds)) ) {
    			cerr << "Unsupported DDS file format: Detected ATI1/BC4 encoded data. (Has to be of type DXT1/BC1, DXT5/BC3, BGRA8 or BGR8).\n";
            } else if ( PF_IS_ATI2((*dds)) || PF_IS_BC5U((*dds)) || PF_IS_BC5S((*dds)) ) {
    			cerr << "Unsupported DDS file format: Detected ATI2/BC5 encoded data. (Has to be of type DXT1/BC1, DXT5/BC3, BGRA8 or BGR8).\n";
            } else {
    			cerr << "Unsupported DDS file format. (Has to be of type DXT1/BC1, DXT5/BC3, BGRA8 or BGR8).\n";
            }
			return -1;
        }

        if ( strayBytes > 0 ) {
    		cerr << "Warning: Stray data in input file.\n";
        }

        tfile = new stringstream(ios_base::out|ios_base::in|ios_base::binary);
        dfile = new stringstream(ios_base::out|ios_base::in|ios_base::binary);
        tfile->write((char *)&pvr,sizeof(PVR_HEADER));

        if ( PF_IS_BGRA8((*dds)) ) {
            uint8_t *s = (src+sizeof(DDS_header));
            for (int32_t c=0; c<actualFileSize; c+=4 ) {
                tfile->put((char)s[c+2]);
                tfile->put((char)s[c+1]);
                tfile->put((char)s[c+0]);
                tfile->put((char)s[c+3]);
            }
        } else if ( PF_IS_BGRX8((*dds)) ) {
            uint8_t *s = (src+sizeof(DDS_header));
            for (int32_t c=0; c<actualFileSize; c+=4 ) {
                tfile->put((char)s[c+2]);
                tfile->put((char)s[c+1]);
                tfile->put((char)s[c+0]);
            }
        } else if ( PF_IS_BGR8((*dds)) ) {
            uint8_t *s = (src+sizeof(DDS_header));
            for (int32_t c=0; c<actualFileSize; c+=3 ) {
                tfile->put((char)s[c+2]);
                tfile->put((char)s[c+1]);
                tfile->put((char)s[c+0]);
            }
        } else if ( PF_IS_SINGLECHANNEL((*dds)) ) {
            uint8_t *s = (src+sizeof(DDS_header));
            for (int32_t c=0; c<actualFileSize; c++) {
                tfile->put((char)s[c+0]);
                tfile->put((char)s[c+0]);
                tfile->put((char)s[c+0]);
            }
        } else {
            tfile->write((char *)(src+sizeof(DDS_header)),actualFileSize);
        }
        tfile->seekg(0,ios_base::beg);

        gCompressedFormats = 1;
		gCheckForAlphaValue = false;

		ofile.open(ofilename,ios::out|ios::binary);
		if ( !ofile.is_open() ) {
			cerr << "Could not open output file. '";
			cerr << ofilename;
			cerr << "'\n\n";
			return -1;
		}
		if ( ofile.bad() ) {
			return false;
		}

        if (PF_IS_DXT5((*dds)) && convert_with_alpha(*dfile, *dfile, *tfile, ofile)) {
			ofile.flush();
			outfilesize += ofile.tellp();
			ofile.close();
			return 0;
        } else if ( convert(*dfile, *dfile, *tfile, *tfile, ofile) ) {
			ofile.flush();
			outfilesize += ofile.tellp();
			ofile.close();
			return 0;
		} 
		ofile.close();
		remove(ofilename);
        return 0;
	}
printusage:
	print_usage();
	return -1;
}
