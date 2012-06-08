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

using namespace std;

// compression settings
bool	gSilent				 = false;		// silent operation
bool	gEncodeRawJXR		 = false;		// Do not encode compressed data, use raw RGBA data and compressed as JXR
int32_t gCompressedFormats	 = 0;			// 0 == all, 1 == dxt, 2 == pvrtc, 3 == etc1 
bool	gStoreRawCompressed	 = true;		// Store raw compressed data, do not attempt to apply JXR compression
bool	gEncodeEmptyMipmap	 = false;		// Store empty mip levels
bool	gCheckForAlphaValue	 = false;		// Check for DXT1/PVRTC alpha channel values

bool	gTrimFlexBitsDefault = true;		// JXR setting 
int32_t gTrimFlexBits		 = 0;			// JXR setting 
bool	gJxrQualityDefault	 = true;		// JXR setting 
int32_t gJxrQuality			 = 0;			// JXR setting 
bool	gJxrFormatDefault	 = true;		// JXR setting 
int32_t gEmbedRangeStart     = 0;
int32_t gEmbedRangeEnd       = 256;

jxr_color_fmt_t gJxrFormat	 = JXR_YUV444;	// JXR setting 

// stats for output
size_t infilesize			 = 0;
size_t outfilesize			 = 0;
size_t outlzmasize			 = 0;
size_t texturew				 = 0;
size_t textureh				 = 0;
size_t texturecomp			 = 3;

enum {
//
	PVR_OGL_RGBA_8888		= 0x12,
	PVR_OGL_RGB_888			= 0x15,
//
	PVR_OGL_PVRTC4			= 0x19,
	PVR_D3D_DXT1			= 0x20,
	PVR_D3D_DXT5			= 0x24,
	PVR_ETC_RGB_4BPP		= 0x36,
	PVR_ATI_RGBA_8BPP		= 0xAC, // Flash specific
//
	PVRTEX_MIPMAP			= (1<<8),
	PVRTEX_TWIDDLE			= (1<<9),
	PVRTEX_BUMPMAP			= (1<<10),
	PVRTEX_TILING			= (1<<11),
	PVRTEX_CUBEMAP			= (1<<12),
	PVRTEX_FALSEMIPCOL		= (1<<13),
	PVRTEX_VOLUME			= (1<<14),
	PVRTEX_FLIPPED			= (1<<16),
    PVRTEX_DDSCUBEMAPORDER  = (1<<17), // Flash specific
    PVRTEX_PVRCUBEMAPORDER  = (1<<18), // Flash specific
// 
	PVRTC4_MIN_TEXWIDTH		= 8
};

struct PVR_HEADER {
	uint32_t		dwHeaderSize;
	uint32_t		dwHeight;
	uint32_t		dwWidth;
	uint32_t		dwMipMapCount;
	uint32_t		dwpfFlags;
	uint32_t		dwTextureDataSize;
	uint32_t		dwBitCount;
	uint32_t		dwRBitMask;
	uint32_t		dwGBitMask;
	uint32_t		dwBBitMask;
	uint32_t		dwAlphaBitMask;
	uint32_t		dwPVR;
	uint32_t		dwNumSurfs;
};

static uint32_t read_uint8(istream &file) {
	return static_cast<uint32_t>(file.get()&0xFF);
}

static uint32_t read_uint16_little(istream &file) {
	return (static_cast<uint8_t>(read_uint8(file))<< 8)|
		   (static_cast<uint8_t>(read_uint8(file))<< 0);
}

static uint32_t read_uint16(istream &file) {
	return (static_cast<uint8_t>(read_uint8(file))<< 0)|
		   (static_cast<uint8_t>(read_uint8(file))<< 8);
}

static uint32_t read_uint24_little(istream &file) {
	return 	(static_cast<uint8_t>(read_uint8(file))<< 0)|
			(static_cast<uint8_t>(read_uint8(file))<< 8)|
			(static_cast<uint8_t>(read_uint8(file))<<16);
}

static uint32_t read_uint32_little(istream &file) {
	return 	(static_cast<uint8_t>(read_uint8(file))<< 0)|
			(static_cast<uint8_t>(read_uint8(file))<< 8)|
			(static_cast<uint8_t>(read_uint8(file))<<16)|
			(static_cast<uint8_t>(read_uint8(file))<<24);
}

static uint32_t read_uint24(istream &file) {
	return 	(static_cast<uint8_t>(read_uint8(file))<<16)|
			(static_cast<uint8_t>(read_uint8(file))<< 8)|
			(static_cast<uint8_t>(read_uint8(file))<< 0);
}

static uint32_t read_uint32(istream &file) {
	return 	(static_cast<uint8_t>(read_uint8(file))<<24)|
			(static_cast<uint8_t>(read_uint8(file))<<16)|
			(static_cast<uint8_t>(read_uint8(file))<< 8)|
			(static_cast<uint8_t>(read_uint8(file))<< 0);
}

static uint64_t read_uint64_little(istream &file) {
	return 	(uint64_t(read_uint32(file))<< 0)|
			(uint64_t(read_uint32(file))<<32);
}

static uint64_t read_uint64(istream &file) {
	return 	(uint64_t(read_uint32(file))<<32)|
			(uint64_t(read_uint32(file))<< 0);
}

static void write_uint8(uint32_t v, ostream &ofile) {
	ofile.put(char(v));
}

static void write_uint16(uint32_t v, ostream &ofile) {
	write_uint8((v>> 8)&0xFF,ofile);
	write_uint8((v>> 0)&0xFF,ofile);
}

static void write_uint32(uint32_t v, ostream &ofile) {
	write_uint8((v>>24)&0xFF,ofile);
	write_uint8((v>>16)&0xFF,ofile);
	write_uint8((v>> 8)&0xFF,ofile);
	write_uint8((v>> 0)&0xFF,ofile);
}

static void write_uint24(uint32_t v, ostream &ofile) {
	if (v>>24) {
		cerr << "Internal error!\n";
		exit(0);
	}
	write_uint8((v>>16)&0xFF,ofile);
	write_uint8((v>> 8)&0xFF,ofile);
	write_uint8((v>> 0)&0xFF,ofile);
}

static void write_uint64(uint64_t v, ostream &ofile) {
	write_uint32(v>>32,ofile);
	write_uint32(v&((uint64_t(1)<<32)-1),ofile);
}

struct ImageData {
	uint32_t size;
	
	bool	  flipped;

	uint8_t  *raw;

	uint8_t  *dxt5_alp;		 // dxt1 color map
	uint8_t  *dxt5_abt;		 // dxt1 bit data
	uint16_t *dxt5_col;		 // dxt1 color map
	uint8_t  *dxt5_bit;		 // dxt1 bit data

	uint16_t *dxt1_col;		 // dxt1 color map
	uint8_t  *dxt1_bit;		 // dxt1 bit data

	uint16_t *pvrtc_col;	 // pvrtc color map
	uint8_t  *pvrtc_d0;		 // pvrtc data top
	uint32_t *pvrtc_d1;		 // pvrtc data bottom

	uint32_t *etc1_col;		// etc1 color 24bit
	uint8_t  *etc1_d0;		// etc1 data top
	uint32_t *etc1_d1;		// etc1 data bottom
};

static unsigned int tile_width_in_MB[4096 * 2] = {0};
static unsigned int tile_height_in_MB[4096 * 2] = {0};

static bool SetJPEGXRCommon(jxr_container_t container, jxr_image_t image, bool alpha, int32_t w, int32_t h) {

	jxr_set_BANDS_PRESENT(image, JXR_BP_ALL);
	jxr_set_TRIM_FLEXBITS(image, gTrimFlexBits);
	jxr_set_OVERLAP_FILTER(image, 0);
	jxr_set_DISABLE_TILE_OVERLAP(image, 1);
	jxr_set_FREQUENCY_MODE_CODESTREAM_FLAG(image, 0);
	jxr_set_PROFILE_IDC(image, 111);
	jxr_set_LEVEL_IDC(image, 255);
	jxr_set_LONG_WORD_FLAG(image, 1);
    jxr_set_ALPHA_IMAGE_PLANE_FLAG(image, alpha ? 1 : 0);

	if ( w < 32 || h < 64 || w*h < 64*64  ) {
		jxr_set_NUM_VER_TILES_MINUS1(image, 1);
		jxr_set_NUM_HOR_TILES_MINUS1(image, 1);
		tile_width_in_MB[0] = 0;
		tile_height_in_MB[1] = 0;
		jxr_set_TILE_WIDTH_IN_MB(image, tile_width_in_MB);
		jxr_set_TILE_HEIGHT_IN_MB(image, tile_height_in_MB);
	} else if ( h < 256 ) {
		jxr_set_NUM_VER_TILES_MINUS1(image, 1);
		jxr_set_NUM_HOR_TILES_MINUS1(image, 4);
		tile_width_in_MB[0]  = w/16;
		tile_height_in_MB[0] = h/16/4;
		tile_height_in_MB[1] = h/16/4;
		tile_height_in_MB[2] = h/16/4;
		tile_height_in_MB[3] = h/16/4;		
		jxr_set_TILE_WIDTH_IN_MB(image, tile_width_in_MB);
		jxr_set_TILE_HEIGHT_IN_MB(image, tile_height_in_MB);
	} else {
		jxr_set_NUM_VER_TILES_MINUS1(image, 1);
		jxr_set_NUM_HOR_TILES_MINUS1(image, 8);
		tile_width_in_MB[0]  = w/16;
		tile_height_in_MB[0] = h/16/8;
		tile_height_in_MB[1] = h/16/8;
		tile_height_in_MB[2] = h/16/8;
		tile_height_in_MB[3] = h/16/8;		
		tile_height_in_MB[4] = h/16/8;		
		tile_height_in_MB[5] = h/16/8;		
		tile_height_in_MB[6] = h/16/8;			
		tile_height_in_MB[7] = h/16/8;		
		jxr_set_TILE_WIDTH_IN_MB(image, tile_width_in_MB);
		jxr_set_TILE_HEIGHT_IN_MB(image, tile_height_in_MB);
	}

    jxr_set_pixel_format(image, jxrc_get_pixel_format(container));
    
    return true;
}

static void SetJPEGXRQuality(jxr_image_t image, int32_t quality)
{
	if (quality == 0 ) {
	    jxr_set_QP_LOSSLESS(image);
	} else {
		if (quality < 16) {
			quality = quality * 2;
		} else if (quality <= 48) {
			quality = quality + 18;
		} else {
			quality = quality + 18 + 2;
		}
	    jxr_set_QP_UNIFORM(image, quality);
	}
}

static bool SetJPEGXRaw(jxr_container_t container, jxr_image_t image, int32_t quality, bool alpha, int32_t w, int32_t h) {

	jxr_set_INTERNAL_CLR_FMT(image, gJxrFormat, 4);
	jxr_set_OUTPUT_CLR_FMT(image, JXR_OCF_RGB);
	jxr_set_OUTPUT_BITDEPTH(image, JXR_BD8);   
	SetJPEGXRCommon(container,image,alpha,w,h);
	SetJPEGXRQuality(image,quality);
    return true;
}

static bool SetJPEG8(jxr_container_t container, jxr_image_t image, int32_t quality, int32_t w, int32_t h) {
	jxr_set_INTERNAL_CLR_FMT(image, JXR_YONLY, 1);
	jxr_set_OUTPUT_CLR_FMT(image, JXR_OCF_YONLY);
	jxr_set_OUTPUT_BITDEPTH(image, JXR_BD8);   
	SetJPEGXRCommon(container,image,false,w,h);
	SetJPEGXRQuality(image,quality);
    return true;
}

static bool SetJPEGX565(jxr_container_t container, jxr_image_t image, int32_t quality, int32_t w, int32_t h) {
	jxr_set_INTERNAL_CLR_FMT(image, gJxrFormat, 1);
	jxr_set_OUTPUT_CLR_FMT(image, JXR_OCF_RGB);
	jxr_set_OUTPUT_BITDEPTH(image, JXR_BD565);   
	SetJPEGXRCommon(container,image,false,w,h);
	SetJPEGXRQuality(image,quality);
    return true;
}

static bool SetJPEGX555(jxr_container_t container, jxr_image_t image, int32_t quality, int32_t w, int32_t h) {
	jxr_set_INTERNAL_CLR_FMT(image, gJxrFormat, 1);
	jxr_set_OUTPUT_CLR_FMT(image, JXR_OCF_RGB);
	jxr_set_OUTPUT_BITDEPTH(image, JXR_BD5);   
	SetJPEGXRCommon(container,image,false,w,h);
	SetJPEGXRQuality(image,quality);
    return true;
}

static bool SetJPEGX888(jxr_container_t container, jxr_image_t image, int32_t quality, int32_t w, int32_t h) {
	jxr_set_INTERNAL_CLR_FMT(image, gJxrFormat, 1);
	jxr_set_OUTPUT_CLR_FMT(image, JXR_OCF_RGB);
	jxr_set_OUTPUT_BITDEPTH(image, JXR_BD8);   
	SetJPEGXRCommon(container,image,false,w,h);
	SetJPEGXRQuality(image,quality);
    return true;
}

static void Read8888Data(jxr_image_t image, int mx, int my, int *data) {
	ImageData *imageData = (ImageData*)jxr_get_user_data(image);
	int32_t w = jxr_get_IMAGE_WIDTH(image);
	int32_t h = jxr_get_IMAGE_HEIGHT(image);
	int32_t n = jxr_get_IMAGE_CHANNELS(image);
	int32_t a = jxr_get_ALPHACHANNEL_FLAG(image);
	n += a;
	for ( int32_t y=0; y<16; y++) {
		int32_t dy = h-((my*16)+y)-1; // always upside up
		if ( !imageData->flipped ) {
			dy = (my*16)+y;
		} 
		for ( int32_t x=0; x<16; x++) {
			int32_t dx = (mx*16) + x;
			if ( dy >= 0 && dy < h && dx < w ) {
				data[(16*y+x)*n+0] = (imageData->raw[((dy*w)+dx)*4+0]);
				data[(16*y+x)*n+1] = (imageData->raw[((dy*w)+dx)*4+1]);
				data[(16*y+x)*n+2] = (imageData->raw[((dy*w)+dx)*4+2]);
				data[(16*y+x)*n+3] = (imageData->raw[((dy*w)+dx)*4+3]);
			} else {
				data[(y*16+x)*n+3] = 0;
				data[(y*16+x)*n+2] = 0;
				data[(y*16+x)*n+1] = 0;
				data[(y*16+x)*n+0] = 0;
			}
		}
	}
}

static void Read888Data(jxr_image_t image, int mx, int my, int *data) {
	ImageData *imageData = (ImageData*)jxr_get_user_data(image);
	int32_t w = jxr_get_IMAGE_WIDTH(image);
	int32_t h = jxr_get_IMAGE_HEIGHT(image);
	int32_t n = jxr_get_IMAGE_CHANNELS(image);
	for ( int32_t y=0; y<16; y++) {
		int32_t dy = h-((my*16)+y)-1; // always upside up
		if ( !imageData->flipped ) {
			dy = (my*16)+y;
		} 
		for ( int32_t x=0; x<16; x++) {
			int32_t dx = (mx*16) + x;
			if ( dy >= 0 && dy < h && dx < w ) {
				data[(16*y+x)*n+0] = (imageData->raw[((dy*w)+dx)*3+0]);
				data[(16*y+x)*n+1] = (imageData->raw[((dy*w)+dx)*3+1]);
				data[(16*y+x)*n+2] = (imageData->raw[((dy*w)+dx)*3+2]);
			} else {
				data[(y*16+x)*n+2] = 0;
				data[(y*16+x)*n+1] = 0;
				data[(y*16+x)*n+0] = 0;
			}
		}
	}
}

static void Read8Data_DXT5(jxr_image_t image, int mx, int my, int *data) {
	ImageData *imageData = (ImageData*)jxr_get_user_data(image);
	int32_t w = jxr_get_IMAGE_WIDTH(image);
	int32_t h = jxr_get_IMAGE_HEIGHT(image);
	int32_t n = jxr_get_IMAGE_CHANNELS(image);
	for ( int32_t y=0; y<16; y++) {
		int32_t dy = (my*16) + y;
		for ( int32_t x=0; x<16; x++) {
			int32_t dx = (mx*16) + x;
			if ( dy < h && dx < w ) {
				uint8_t p = imageData->dxt5_alp[(dy*w)+dx];
				data[(y*16+x)*n+0] = p;
			} else {
				data[(y*16+x)*n+0] = 0;
			}
		}
	}
}

static void Read565Data_DXT5(jxr_image_t image, int mx, int my, int *data) {
	ImageData *imageData = (ImageData*)jxr_get_user_data(image);
	int32_t w = jxr_get_IMAGE_WIDTH(image);
	int32_t h = jxr_get_IMAGE_HEIGHT(image);
	int32_t n = jxr_get_IMAGE_CHANNELS(image);
	for ( int32_t y=0; y<16; y++) {
		int32_t dy = (my*16) + y;
		for ( int32_t x=0; x<16; x++) {
			int32_t dx = (mx*16) + x;
			if ( dy < h && dx < w ) {
				uint32_t p = imageData->dxt5_col[(dy*w)+dx];
				if ( p ) {	
					int32_t r = ( p >> 11 ) & 0x1F;
					int32_t g = ( p >>  5 ) & 0x3F;
					int32_t b = ( p >>  0 ) & 0x1F;
					// Bug in JPEG-XR encoder: it wants 666 instead of 565
					r = (r<<1) | (r>>4);
					b = (b<<1) | (b>>4);
					data[(y*16+x)*n+2] = r;
					data[(y*16+x)*n+1] = g;
					data[(y*16+x)*n+0] = b;
				} else {
					data[(y*16+x)*n+2] = 0;
					data[(y*16+x)*n+1] = 0;
					data[(y*16+x)*n+0] = 0;
				}
			} else {
				data[(y*16+x)*n+2] = 0;
				data[(y*16+x)*n+1] = 0;
				data[(y*16+x)*n+0] = 0;
			}
		}
	}
}

static void Read565Data_DXT1(jxr_image_t image, int mx, int my, int *data) {
	ImageData *imageData = (ImageData*)jxr_get_user_data(image);
	int32_t w = jxr_get_IMAGE_WIDTH(image);
	int32_t h = jxr_get_IMAGE_HEIGHT(image);
	int32_t n = jxr_get_IMAGE_CHANNELS(image);
	for ( int32_t y=0; y<16; y++) {
		int32_t dy = (my*16) + y;
		for ( int32_t x=0; x<16; x++) {
			int32_t dx = (mx*16) + x;
			if ( dy < h && dx < w ) {
				uint32_t p = imageData->dxt1_col[(dy*w)+dx];
				if ( p ) {	
					int32_t r = ( p >> 11 ) & 0x1F;
					int32_t g = ( p >>  5 ) & 0x3F;
					int32_t b = ( p >>  0 ) & 0x1F;
					// Bug in JPEG-XR encoder: it wants 666 instead of 565
					r = (r<<1) | (r>>4);
					b = (b<<1) | (b>>4);
					data[(y*16+x)*n+2] = r;
					data[(y*16+x)*n+1] = g;
					data[(y*16+x)*n+0] = b;
				} else {
					data[(y*16+x)*n+2] = 0;
					data[(y*16+x)*n+1] = 0;
					data[(y*16+x)*n+0] = 0;
				}
			} else {
				data[(y*16+x)*n+2] = 0;
				data[(y*16+x)*n+1] = 0;
				data[(y*16+x)*n+0] = 0;
			}
		}
	}
}

static void twiddle(int32_t &r, int32_t u, int32_t v, int32_t w, int32_t h)
{
	r = 0;
	int32_t mins;
	int32_t maxv;
	if(h < w) {
		mins = h;
		maxv = u;
	} else {
		mins = w;
		maxv = v;
	}
	int32_t a = 1;
	int32_t b = 1;
	int32_t c = 0;
	for ( int32_t a=1; a<mins;) {
		if(u & a) {
			r |= (b << 1);
		}
		if(v & a) {
			r |= b;
		}
		a <<= 1;
		b <<= 2;
		c  += 1;
	}
	maxv >>= c;
	r|=(maxv<<(2*c));
	if ( r >= w * h ) {
		int32_t d=0; d=0;
	}
}

static void Read555Data_PVRTC(jxr_image_t image, int mx, int my, int *data) {
	ImageData *imageData = (ImageData*)jxr_get_user_data(image);
	int32_t w = jxr_get_IMAGE_WIDTH(image);
	int32_t h = jxr_get_IMAGE_HEIGHT(image);
	int32_t n = jxr_get_IMAGE_CHANNELS(image);
	
	for ( int32_t y=0; y<16; y++) {
		int32_t dy = (my*16) + y;
		if ( dy < h / 2 ) {
			for ( int32_t x=0; x<16; x++) {
				int32_t dx = (mx*16) + x;
				if ( dy < h && dx < w ) {
					int32_t aa = 0;
					twiddle(aa,dx,dy,w,h/2);
					uint32_t p = imageData->pvrtc_col[aa];
					data[(y*16+x)*n+2] = ( p >> 10 ) & 0x1F;
					data[(y*16+x)*n+1] = ( p >>  5 ) & 0x1F;
					data[(y*16+x)*n+0] = ( p >>  0 ) & 0x1F;
				} else {
					data[(y*16+x)*n+2] = 0;
					data[(y*16+x)*n+1] = 0;
					data[(y*16+x)*n+0] = 0;
				}
			}
		} else {
			dy -= h / 2;
			for ( int32_t x=0; x<16; x++) {
				int32_t dx = (mx*16) + x;
				if ( dy < h && dx < w ) {
					int32_t aa = 0;
					twiddle(aa,dx,dy,w,h/2);
					uint32_t p = imageData->pvrtc_col[aa+(h/2*w)];
					data[(y*16+x)*n+2] = ( p >> 10 ) & 0x1F;
					data[(y*16+x)*n+1] = ( p >>  5 ) & 0x1F;
					data[(y*16+x)*n+0] = ( ( p >>  0 ) & 0x1E ) | ( ( ( p >>  0 ) & 0x1F ) >> 4 );
				} else {
					data[(y*16+x)*n+2] = 0;
					data[(y*16+x)*n+1] = 0;
					data[(y*16+x)*n+0] = 0;
				}
			}
		}
	}
}

static uint32_t extend_3_to_5(uint32_t a) { return ((a<<2)|(a>>3)); }
static uint32_t extend_4_to_5(uint32_t a) { return ((a<<1)|(a>>3)); }

static void Read555Data_ETC1(jxr_image_t image, int mx, int my, int *data) {
	ImageData *imageData = (ImageData*)jxr_get_user_data(image);
	int32_t w = jxr_get_IMAGE_WIDTH(image);
	int32_t h = jxr_get_IMAGE_HEIGHT(image);
	int32_t n = jxr_get_IMAGE_CHANNELS(image);
	for ( int32_t y=0; y<16; y++) {
		int32_t dy = (my*16) + y;
		if ( dy < h / 2 ) {
			for ( int32_t x=0; x<16; x++) {
				int32_t dx = (mx*16) + x;
				if ( dy < h && dx < w ) {
					int32_t bx = dx;
					int32_t by = dy;
					int32_t diff = ( imageData->etc1_d0[by*w+bx] & 2 ) ? true : false;
					if ( diff ) {
						data[(y*16+x)*n+2] = ( imageData->etc1_col[by*w+bx]&0xf80000) >> (16+3);
						data[(y*16+x)*n+1] = ( imageData->etc1_col[by*w+bx]&0x00f800) >> ( 8+3);
						data[(y*16+x)*n+0] = ( imageData->etc1_col[by*w+bx]&0x0000f8) >> ( 0+3);
					} else {
						data[(y*16+x)*n+2] = extend_4_to_5(( imageData->etc1_col[by*w+bx]&0xf00000) >> (16+4));
						data[(y*16+x)*n+1] = extend_4_to_5(( imageData->etc1_col[by*w+bx]&0x00f000) >> ( 8+4));
						data[(y*16+x)*n+0] = extend_4_to_5(( imageData->etc1_col[by*w+bx]&0x0000f0) >> ( 0+4));
					}
				} else {
					data[(y*16+x)*n+2] = 0;
					data[(y*16+x)*n+1] = 0;
					data[(y*16+x)*n+0] = 0;
				}
			}
		} else {
			dy -= h / 2;
			for ( int32_t x=0; x<16; x++) {
				int32_t dx = (mx*16) + x;
				if ( dy < h && dx < w ) {
					int32_t bx = dx;
					int32_t by = dy;
					int32_t diff = ( imageData->etc1_d0[by*w+bx] & 2 ) ? true : false;
					if ( diff ) {
						int32_t r0 = ( imageData->etc1_col[by*w+bx]&0xf80000) >> (16+3);
						int32_t g0 = ( imageData->etc1_col[by*w+bx]&0x00f800) >> ( 8+3);
						int32_t b0 = ( imageData->etc1_col[by*w+bx]&0x0000f8) >> ( 0+3);
						int32_t r1 = r0 + ((int8_t( ( imageData->etc1_col[by*w+bx]&0x070000) >> 11 ) >> 5));
						int32_t g1 = g0 + ((int8_t( ( imageData->etc1_col[by*w+bx]&0x000700) >>  3 ) >> 5));
						int32_t b1 = b0 + ((int8_t( ( imageData->etc1_col[by*w+bx]&0x000007) <<  5 ) >> 5));
						data[(y*16+x)*n+2] = r1;
						data[(y*16+x)*n+1] = g1;
						data[(y*16+x)*n+0] = b1;
					} else {
						data[(y*16+x)*n+2] = extend_4_to_5( ( imageData->etc1_col[by*w+bx]&0x0f0000) >> (12+4) );
						data[(y*16+x)*n+1] = extend_4_to_5( ( imageData->etc1_col[by*w+bx]&0x000f00) >> ( 4+4) );
						data[(y*16+x)*n+0] = extend_4_to_5( ( imageData->etc1_col[by*w+bx]&0x00000f) << ( 4-4) );
					}
				} else {
					data[(y*16+x)*n+2] = 0;
					data[(y*16+x)*n+1] = 0;
					data[(y*16+x)*n+0] = 0;
				}
			}
		}
	}
}

bool validate_texture(PVR_HEADER &pvr_header)
{
	if ( (pvr_header.dwpfFlags & PVRTEX_FLIPPED) ) {
		cerr << "pvrtc textures must not be flipped.\n\n(Hint: In PVRTexTool make sure that the 'Vertically Flip For This API' checkbox is un-checked or use the '-yflip0' commandline option )\n";
		return false;
	}
	if ( ( pvr_header.dwpfFlags & 0xFF ) != PVR_OGL_PVRTC4 && ( pvr_header.dwpfFlags & PVRTEX_TWIDDLE )) {
		cerr << "Twiddled non pvrtc textures not supported!\n\n";
		return false;
	} 
	if ( ( pvr_header.dwpfFlags & 0xFF ) == PVR_OGL_PVRTC4 && ( pvr_header.dwpfFlags & PVRTEX_TWIDDLE ) == 0 ) {
		cerr << "Pvrtc textures need to be twiddled!\n\n(Hint: Do not use the -nt option in PVRTexTool)\n";
		return false;
	} 
	if ( pvr_header.dwpfFlags & PVRTEX_BUMPMAP) {
		cerr << "Bumpmap pvr textures not supported!\n\n";
		return false;
	} 
	if ( pvr_header.dwpfFlags & PVRTEX_TILING) {
		cerr << "Tiled pvr textures not supported!\n\n";
		return false;
	} 
	if ( pvr_header.dwpfFlags & PVRTEX_FALSEMIPCOL) {
		cerr << "False mipmap color pvr textures not supported!\n\n";
		return false;
	} 
	if ( pvr_header.dwpfFlags & PVRTEX_VOLUME) {
		cerr << "Volume pvr textures not supported!\n\n";
		return false;
	} 
	return true;
}

size_t LzmaSlowCompress(uint8_t *src, uint8_t *dst, size_t len)
{
	size_t  sln = 0x7FFFFFFF;
	int32_t slc = 3;
	int32_t spb = 2;

	if ( !gSilent ) {
		cout << ".";
		cout.flush();
	}

	size_t bufferLen = len*2+4096;
	size_t propsLen = LZMA_PROPS_SIZE;
	int res = LzmaCompress(dst+LZMA_PROPS_SIZE,&bufferLen,(const unsigned char *)src,len,(unsigned char *)dst,&propsLen,9,1<<20,slc,0,spb,273,1);
	return bufferLen+LZMA_PROPS_SIZE;
}

bool read_pvr(istream &file, PVR_HEADER &pvr_header) {
	pvr_header.dwHeaderSize = read_uint32_little(file);
	pvr_header.dwHeight = read_uint32_little(file);
	pvr_header.dwWidth = read_uint32_little(file);
	pvr_header.dwMipMapCount = read_uint32_little(file);
	pvr_header.dwpfFlags = read_uint32_little(file);
	pvr_header.dwTextureDataSize = read_uint32_little(file);
	pvr_header.dwBitCount = read_uint32_little(file);
	pvr_header.dwRBitMask = read_uint32_little(file);
	pvr_header.dwGBitMask = read_uint32_little(file);
	pvr_header.dwBBitMask = read_uint32_little(file);
	pvr_header.dwAlphaBitMask = read_uint32_little(file);
	pvr_header.dwPVR = read_uint32_little(file);
	pvr_header.dwNumSurfs = read_uint32_little(file);
	return true;
}

enum {
	ATF_FORMAT_888			     = 0x00,
	ATF_FORMAT_8888			     = 0x01,
	ATF_FORMAT_COMPRESSED	     = 0x02,
	ATF_FORMAT_COMPRESSEDRAW     = 0x03,
	ATF_FORMAT_COMPRESSEDALPHA   = 0x04,
	ATF_FORMAT_COMPRESSEDRAWALPHA= 0x05,
	ATF_FORMAT_CUBEMAP		     = 0x80,
};

//
// ATF format:
//
// U8[3]   -  signature  - 'ATF'
// U24     -  len        - length in bytes of ATF file.
// U1	   -  cubemap	 - 0=normal, 1=cube map
// U7      -  format     - 0=RGB888, 1=RGBA8888 (straight alpha), 2=Compressed(dxt1+pvrtc+etc1)
// U8	   -  width      - texture size (2^n) (max n=11)
// U8	   -  height     - texture size (2^n) (max n=11)
// U8      -  count      - total texture count (main + mip maps, max n=12)
//
// count * [ // for format 0 and 1
// U24     -  len        - length in bytes of JPEG-XR
// U8[len] -  data		 - JPEG-XR data (JXRC_FMT_24bppRGB, JXRC_FMT_32bppBGRA)
// ]
//
// count * [ // for format 2
// U24     -  len        - length in bytes of DXT1 data
// U8[len] -  data		 - LZMA compressed DXT1 data
// U24     -  len        - length in bytes of JPEG-XR
// U8[len] -  data		 - JPEG-XR data (JXRC_FMT_16bppBGR565)
//
// U24     -  len        - length in bytes of PVRTC (4bpp) top data
// U8[len] -  data		 - LZMA compressed PVRTC data
// U24     -  len        - length in bytes of PVRTC (4bpp) bottom data
// U8[len] -  data		 - LZMA compressed PVRTC data
// U24     -  len        - length in bytes of JPEG-XR
// U8[len] -  data		 - JPEG-XR data (JXRC_FMT_16bppBGR555)
//
// U24     -  len        - length in bytes of ETC1 top data
// U8[len] -  data		 - LZMA compressed ETC1 top data
// U24     -  len        - length in bytes of ETC1 bottom data
// U8[len] -  data		 - LZMA compressed ETC1 bottom data
// U24     -  len        - length in bytes of JPEG-XR
// U8[len] -  data		 - JPEG-XR data (JXRC_FMT_16bppBGR555)
// ]
//

static void write_debug_image(jxr_container_t container)
{
	static int32_t count = 0;
	std::ostringstream s;
	s << "debug_";
	s << count++;
	s << ".jxr";
	ofstream file;
	string fname = s.str();
	file.open(fname.c_str(),ios::out|ios::binary);
	file.write((const char *)container->wb.buffer(),container->wb.len());
	file.close();
}

static void write_header(int32_t w, int32_t h, uint8_t format, int32_t textureCount, ostream &ofile)
{
	ofile.put('A');
	ofile.put('T');
	ofile.put('F');
	ofile.put(uint8_t(0)); // placeholder for size
	ofile.put(uint8_t(0)); // placeholder for size
	ofile.put(uint8_t(0)); // placeholder for size
	ofile.put(uint8_t(format));
	int32_t wsizelog2 =	(((w & 0xAAAAAAAA)?1:0)     )|
						(((w & 0xCCCCCCCC)?1:0) << 1)|
						(((w & 0xF0F0F0F0)?1:0) << 2)|
						(((w & 0xFF00FF00)?1:0) << 3)|
						(((w & 0xFFFF0000)?1:0) << 4);
	ofile.put(uint8_t(wsizelog2));
	int32_t hsizelog2 =	(((h & 0xAAAAAAAA)?1:0)     )|
						(((h & 0xCCCCCCCC)?1:0) << 1)|
						(((h & 0xF0F0F0F0)?1:0) << 2)|
						(((h & 0xFF00FF00)?1:0) << 3)|
						(((h & 0xFFFF0000)?1:0) << 4);
	ofile.put(uint8_t(hsizelog2));
	ofile.put(uint8_t(textureCount));
}

static bool write_dxt1(int32_t w, int32_t h, int32_t level, bool flipped, istream &ifile, ostream &ofile)
{
	if ( ( gCompressedFormats == 0 || gCompressedFormats == 1 ) && !(level < gEmbedRangeStart || level > gEmbedRangeEnd ) ) {

		if ( gStoreRawCompressed ) {

			uint32_t tsize = max(1,w/4)*max(1,h/4)*sizeof(uint32_t)*2;
			write_uint24(tsize,ofile);

			for ( int32_t d=0; d<tsize; d++) {
				write_uint8(read_uint8(ifile),ofile);
			}

		} else {
			ImageData imageData;
			imageData.flipped = flipped;
			imageData.dxt1_col = new uint16_t[max(2,(w/4))*max(2,(h/4)*2)];
			uint16_t *cl0 = imageData.dxt1_col;
			uint16_t *cl1 = imageData.dxt1_col + max(1,w/4)*max(1,h/4);
			imageData.dxt1_bit = new uint8_t[max(1,w/4)*max(1,h/4)*4];
			uint8_t *bit = imageData.dxt1_bit;
			for ( int32_t d=0; d<max(1,w/4)*max(1,h/4); d++) {
				if ( gEncodeEmptyMipmap && level > 0 ) {
					*cl0++ = 0;
					*cl1++ = 0;
					*bit++ = 0;
					*bit++ = 0;
					*bit++ = 0;
					*bit++ = 0;
				} else {
					uint16_t c0 = read_uint16(ifile);
					*cl0++ = c0;
					uint16_t c1 = read_uint16(ifile);
					*cl1++ = c1;
					if ( gCheckForAlphaValue && c0 < c1 ) {
						cerr << "DXT1 textures with alpha not supported!\n\n";
						return false;
					}
					*bit++ = read_uint8(ifile);
					*bit++ = read_uint8(ifile);
					*bit++ = read_uint8(ifile);
					*bit++ = read_uint8(ifile);
				}
			}

			{
				uint8_t *buffer = new uint8_t[max(1,w/4)*max(1,h/4)*sizeof(uint32_t)*2+LZMA_PROPS_SIZE+4096];

				size_t bufferLen = LzmaSlowCompress((uint8_t*)imageData.dxt1_bit, buffer, max(1,w/4)*max(1,h/4)*sizeof(uint32_t));
				
				write_uint24(bufferLen,ofile);

				ofile.write((const char *)buffer,bufferLen);
				outlzmasize += bufferLen;

				delete [] buffer;
			}

			jxr_container_t container = jxr_create_container();
			jxrc_start_file(container);

			if ( jxrc_begin_ifd_entry(container) != 0 ) {
				cerr << "Could not create ATF file!\n\n";
				return false;
			}
			jxrc_set_pixel_format(container, JXRC_FMT_16bppBGR565);
			jxrc_set_image_shape(container, max(1,w/4), max(2,h/2));
			jxrc_set_separate_alpha_image_plane(container, 0);
			jxrc_set_image_band_presence(container, JXR_BP_ALL);
			static unsigned char window_params[5] = {0,0,0,0,0};
			jxr_image_t image = jxr_create_image(max(1,w/4), max(2,h/2), window_params);

			if ( !image ) {
				return false;
			}

			SetJPEGX565(container,image,gJxrQuality, max(1,w/4), max(2,h/2));

			jxrc_begin_image_data(container);
			jxr_set_block_input(image, Read565Data_DXT1);  
			jxr_set_user_data(image, &imageData);

			if ( jxr_write_image_bitstream(image,container) != 0 ) {
				cerr << "JPEGXR encoding error!\n\n";
				return false;
			}

			jxr_destroy(image); 

			jxrc_write_container_post(container);

			//write_debug_image(container);

			write_uint24(container->wb.len(),ofile);
			ofile.write((const char *)container->wb.buffer(),container->wb.len());
			
			jxr_destroy_container(container);
			
			delete [] imageData.dxt1_col;
			delete [] imageData.dxt1_bit;
		}
	} else {
		if ( gStoreRawCompressed ) {
			write_uint24(0,ofile);
		} else {
			write_uint24(0,ofile);
			write_uint24(0,ofile);
		}
		for ( int32_t d=0; d<max(1,w/4)*max(1,h/4); d++) {
            read_uint32(ifile);
            read_uint32(ifile);
        }
	}
	return true;
}

static bool write_dxt5(int32_t w, int32_t h, int32_t level, bool flipped, istream &ifile, ostream &ofile)
{
	if ( ( gCompressedFormats == 0 || gCompressedFormats == 1 ) && !(level < gEmbedRangeStart || level > gEmbedRangeEnd ) ) {

		if ( gStoreRawCompressed ) {

			uint32_t tsize = max(1,w/4)*max(1,h/4)*sizeof(uint32_t)*4;
			write_uint24(tsize,ofile);
			for ( int32_t d=0; d<tsize; d++) {
				write_uint8(read_uint8(ifile),ofile);
			}

		} else {

			ImageData imageData;
			imageData.flipped = flipped;
			imageData.dxt5_alp = new uint8_t[max(2,(w/4))*max(2,(h/4)*2)];
			imageData.dxt5_col = new uint16_t[max(2,(w/4))*max(2,(h/4)*2)];
			uint8_t *al0 = imageData.dxt5_alp;
			uint8_t *al1= imageData.dxt5_alp + max(1,w/4)*max(1,h/4);
			uint16_t *cl0 = imageData.dxt5_col;
			uint16_t *cl1 = imageData.dxt5_col + max(1,w/4)*max(1,h/4);
			imageData.dxt5_abt = new uint8_t[max(1,w/4)*max(1,h/4)*6];
			imageData.dxt5_bit = new uint8_t[max(1,w/4)*max(1,h/4)*4];
			uint8_t *abt = (uint8_t *)imageData.dxt5_abt;
			uint8_t *bit = (uint8_t *)imageData.dxt5_bit;
			for ( int32_t d=0; d<max(1,w/4)*max(1,h/4); d++) {
				if ( gEncodeEmptyMipmap && level > 0 ) {
					*al0++ = 0;
					*al1++ = 0;
					*abt++ = 0;
					*abt++ = 0;
					*abt++ = 0;
					*abt++ = 0;
					*abt++ = 0;
					*abt++ = 0;

					*cl0++ = 0;
					*cl1++ = 0;
					*bit++ = 0;
					*bit++ = 0;
					*bit++ = 0;
					*bit++ = 0;
				} else {
					uint8_t a0 = read_uint8(ifile);
					*al0++ = a0;
					uint8_t a1 = read_uint8(ifile);
					*al1++ = a1;

					*abt++ = read_uint8(ifile);
					*abt++ = read_uint8(ifile);
					*abt++ = read_uint8(ifile);
					*abt++ = read_uint8(ifile);
					*abt++ = read_uint8(ifile);
					*abt++ = read_uint8(ifile);

					uint16_t c0 = read_uint16(ifile);
					*cl0++ = c0;
					uint16_t c1 = read_uint16(ifile);
					*cl1++ = c1;
					*bit++ = read_uint8(ifile);
					*bit++ = read_uint8(ifile);
					*bit++ = read_uint8(ifile);
					*bit++ = read_uint8(ifile);
				}
			}

			{
				uint8_t *buffer = new uint8_t[max(1,w/4)*max(1,h/4)*sizeof(uint32_t)*8+LZMA_PROPS_SIZE+4096];

				size_t bufferLen = LzmaSlowCompress((uint8_t*)imageData.dxt5_abt, buffer, max(1,w/4)*max(1,h/4)*6);
				
				write_uint24(bufferLen,ofile);

				ofile.write((const char *)buffer,bufferLen);
				outlzmasize += bufferLen;

				delete [] buffer;
			}

			{
				jxr_container_t container = jxr_create_container();
				jxrc_start_file(container);

				if ( jxrc_begin_ifd_entry(container) != 0 ) {
					cerr << "Could not create ATF file!\n\n";
					return false;
				}
				jxrc_set_pixel_format(container, JXRC_FMT_8bppGray);
				jxrc_set_image_shape(container, max(1,w/4), max(2,h/2));
				jxrc_set_separate_alpha_image_plane(container, 0);
				jxrc_set_image_band_presence(container, JXR_BP_ALL);
				static unsigned char window_params[5] = {0,0,0,0,0};
				jxr_image_t image = jxr_create_image(max(1,w/4), max(2,h/2), window_params);

				if ( !image ) {
					return false;
				}

				SetJPEG8(container,image,gJxrQuality, max(1,w/4), max(2,h/2));

				jxrc_begin_image_data(container);
				jxr_set_block_input(image, Read8Data_DXT5);  
				jxr_set_user_data(image, &imageData);

				if ( jxr_write_image_bitstream(image,container) != 0 ) {
					cerr << "JPEGXR encoding error!\n\n";
					return false;
				}

				jxr_destroy(image); 

				jxrc_write_container_post(container);

				//write_debug_image(container);

				write_uint24(container->wb.len(),ofile);
				ofile.write((const char *)container->wb.buffer(),container->wb.len());
				
				jxr_destroy_container(container);
			}

			{
				uint8_t *buffer = new uint8_t[max(1,w/4)*max(1,h/4)*sizeof(uint32_t)*8+LZMA_PROPS_SIZE+4096];

				size_t bufferLen = LzmaSlowCompress((uint8_t*)imageData.dxt5_bit, buffer, max(1,w/4)*max(1,h/4)*4);
				
				write_uint24(bufferLen,ofile);

				ofile.write((const char *)buffer,bufferLen);
				outlzmasize += bufferLen;

				delete [] buffer;
			}

			{
				jxr_container_t container = jxr_create_container();
				jxrc_start_file(container);

				if ( jxrc_begin_ifd_entry(container) != 0 ) {
					cerr << "Could not create ATF file!\n\n";
					return false;
				}
				jxrc_set_pixel_format(container, JXRC_FMT_16bppBGR565);
				jxrc_set_image_shape(container, max(1,w/4), max(2,h/2));
				jxrc_set_separate_alpha_image_plane(container, 0);
				jxrc_set_image_band_presence(container, JXR_BP_ALL);
				static unsigned char window_params[5] = {0,0,0,0,0};
				jxr_image_t image = jxr_create_image(max(1,w/4), max(2,h/2), window_params);

				if ( !image ) {
					return false;
				}

				SetJPEGX565(container,image,gJxrQuality, max(1,w/4), max(2,h/2));

				jxrc_begin_image_data(container);
				jxr_set_block_input(image, Read565Data_DXT5);  
				jxr_set_user_data(image, &imageData);

				if ( jxr_write_image_bitstream(image,container) != 0 ) {
					cerr << "JPEGXR encoding error!\n\n";
					return false;
				}

				jxr_destroy(image); 

				jxrc_write_container_post(container);

				//write_debug_image(container);

				write_uint24(container->wb.len(),ofile);
				ofile.write((const char *)container->wb.buffer(),container->wb.len());
				
				jxr_destroy_container(container);
			}
			
			delete [] imageData.dxt5_alp;
			delete [] imageData.dxt5_abt;
			delete [] imageData.dxt5_col;
			delete [] imageData.dxt5_bit;
		}
	} else {
		if ( gStoreRawCompressed ) {
			write_uint24(0,ofile);
		} else {
			write_uint24(0,ofile);
			write_uint24(0,ofile);
			write_uint24(0,ofile);
			write_uint24(0,ofile);
		}
		for ( int32_t d=0; d<max(1,w/4)*max(1,h/4); d++) {
            read_uint32(ifile);
            read_uint32(ifile);
            read_uint32(ifile);
            read_uint32(ifile);
        }
	}
	return true;
}

static bool write_pvrtc_alpha(int32_t w, int32_t h, int32_t level, bool flipped, istream &ifile, ostream &ofile)
{
	int32_t pw = max(int32_t(PVRTC4_MIN_TEXWIDTH),w);
	int32_t ph = max(int32_t(PVRTC4_MIN_TEXWIDTH),h);

	if ( ( gCompressedFormats == 0 || gCompressedFormats == 3 ) && !(level < gEmbedRangeStart || level > gEmbedRangeEnd ) ) {

        if ( gStoreRawCompressed ) {

			uint32_t tsize = max(1,pw/4)*max(1,ph/4)*sizeof(uint32_t)*2;
			write_uint24(tsize,ofile);
			for ( int32_t d=0; d<tsize; d++) {
				write_uint8(read_uint8(ifile),ofile);
			}

		} else {
			ImageData imageData;
			imageData.flipped = flipped;
			imageData.pvrtc_col = new uint16_t[max(2,pw/4)*max(2,ph/4)*2];
			uint16_t *cl0 = imageData.pvrtc_col;
			uint16_t *cl1 = imageData.pvrtc_col + max(1,pw/4)*max(1,ph/4);
			imageData.pvrtc_d0 = new uint8_t[max(1,pw/4)*max(1,ph/4)];
			uint8_t *d0 = (uint8_t *)imageData.pvrtc_d0;
			imageData.pvrtc_d1 = new uint32_t[max(1,pw/4)*max(1,ph/4)];
			uint8_t *d1 = (uint8_t *)imageData.pvrtc_d1;
			
			for ( int32_t d=0; d<max(1,pw/4)*max(1,ph/4); d++) {
				if ( gEncodeEmptyMipmap && level > 0 ) {
					*d1++ = 0;
					*d1++ = 0;
					*d1++ = 0;
					*d1++ = 0;
					*cl0++ = 0;
					*d0++ = 0;
					*cl1++ = 0;
				} else {
					*d1++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
					uint16_t c0 = read_uint16(ifile);
					*cl0++ = c0;
					uint16_t c1 = read_uint16(ifile);
					*d0++ = ( ( c0 & 1 ) ? 1 : 0 ) | ( ( c0 & 0x8000 ) ? 2 : 0 ) | ( ( c1 & 0x8000 ) ? 4 : 0 );
					*cl1++ = c1;
				}
			}

			{ // pvrtc d1
				uint8_t *buffer = new uint8_t[max(1,pw/4)*max(1,ph/4)*sizeof(uint8_t)*2+LZMA_PROPS_SIZE+4096];

				size_t bufferLen = LzmaSlowCompress((uint8_t*)imageData.pvrtc_d0, buffer, max(1,pw/4)*max(1,ph/4)*sizeof(uint8_t));

				write_uint24(bufferLen,ofile);

				ofile.write((const char *)buffer,bufferLen);
				outlzmasize += bufferLen;
				delete [] buffer;
			}
			
			{ // pvrtc d1
				uint8_t *buffer = new uint8_t[max(1,pw/4)*max(1,ph/4)*sizeof(uint32_t)*2+LZMA_PROPS_SIZE+4096];

				size_t bufferLen = LzmaSlowCompress((uint8_t*)imageData.pvrtc_d1, buffer, max(1,pw/4)*max(1,ph/4)*sizeof(uint32_t));

				write_uint24(bufferLen,ofile);

				ofile.write((const char *)buffer,bufferLen);
				outlzmasize += bufferLen;
				delete [] buffer;
			}

			jxr_container_t container = jxr_create_container();
			jxrc_start_file(container);

			if ( jxrc_begin_ifd_entry(container) != 0 ) {
				cerr << "Could not create ATF file!\n\n";
				return false;
			}
			jxrc_set_pixel_format(container, JXRC_FMT_16bppBGR555);
			jxrc_set_image_shape(container, max(1,pw/4), max(2,ph/2));
			jxrc_set_separate_alpha_image_plane(container, 0);
			jxrc_set_image_band_presence(container, JXR_BP_ALL);
			static unsigned char window_params[5] = {0,0,0,0,0};
			jxr_image_t image = jxr_create_image(max(1,pw/4), max(2,ph/2), window_params);

			if ( !image ) {
				return false;
			}

			SetJPEGX555(container,image,gJxrQuality, max(1,pw/4), max(2,ph/2));

			jxrc_begin_image_data(container);
			jxr_set_block_input(image, Read555Data_PVRTC);  
			jxr_set_user_data(image, &imageData);

			if ( jxr_write_image_bitstream(image,container) != 0 ) {
				cerr << "JPEGXR encoding error!\n";
				return false;
			}

			jxr_destroy(image); 

			jxrc_write_container_post(container);

			//write_debug_image(container);
			
			write_uint24(container->wb.len(),ofile);
			ofile.write((const char *)container->wb.buffer(),container->wb.len());
			
			jxr_destroy_container(container);
			
			delete [] imageData.pvrtc_col;
			delete [] imageData.pvrtc_d0;
			delete [] imageData.pvrtc_d1;
		}
	} else {
		if ( gStoreRawCompressed ) {
			write_uint24(0,ofile);
		} else {
			write_uint24(0,ofile);
			write_uint24(0,ofile);
			write_uint24(0,ofile);
		}
		for ( int32_t d=0; d<max(1,pw/4)*max(1,ph/4); d++) {
            read_uint32(ifile);
            read_uint32(ifile);
        }
	}
	return true;
}


static bool write_pvrtc(int32_t w, int32_t h, int32_t level, bool flipped, istream &ifile, ostream &ofile)
{
	int32_t pw = max(int32_t(PVRTC4_MIN_TEXWIDTH),w);
	int32_t ph = max(int32_t(PVRTC4_MIN_TEXWIDTH),h);

	if ( ( gCompressedFormats == 0 || gCompressedFormats == 3 ) && !(level < gEmbedRangeStart || level > gEmbedRangeEnd ) ) {

        if ( gStoreRawCompressed ) {

			uint32_t tsize = max(1,pw/4)*max(1,ph/4)*sizeof(uint32_t)*2;
			write_uint24(tsize,ofile);
			for ( int32_t d=0; d<tsize; d++) {
				write_uint8(read_uint8(ifile),ofile);
			}

		} else {
			ImageData imageData;
			imageData.flipped = flipped;
			imageData.pvrtc_col = new uint16_t[max(2,pw/4)*max(2,ph/4)*2];
			uint16_t *cl0 = imageData.pvrtc_col;
			uint16_t *cl1 = imageData.pvrtc_col + max(1,pw/4)*max(1,ph/4);
			imageData.pvrtc_d0 = new uint8_t[max(1,pw/4)*max(1,ph/4)];
			uint8_t *d0 = (uint8_t *)imageData.pvrtc_d0;
			imageData.pvrtc_d1 = new uint32_t[max(1,pw/4)*max(1,ph/4)];
			uint8_t *d1 = (uint8_t *)imageData.pvrtc_d1;
			
			for ( int32_t d=0; d<max(1,pw/4)*max(1,ph/4); d++) {
				if ( gEncodeEmptyMipmap && level > 0 ) {
					*d1++ = 0;
					*d1++ = 0;
					*d1++ = 0;
					*d1++ = 0;
					*cl0++ = 0;
					*d0++ = 0;
					*cl1++ = 0;
				} else {
					*d1++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
					uint16_t c0 = read_uint16(ifile);
					if ( gCheckForAlphaValue && ( c0 & 0x8000 ) == 0 ) {
						cerr << "PVRTC textures with alpha not supported!\n\n";
						return false;
					}
					*cl0++ = c0;
					uint16_t c1 = read_uint16(ifile);
					if ( gCheckForAlphaValue && ( c1 & 0x8000 ) == 0 ) {
						cerr << "PVRTC textures with alpha not supported!\n\n";
						return false;
					}
					*d0++ = ( c0 & 1 );
					*cl1++ = c1;
				}
			}

			{ // pvrtc d1
				uint8_t *buffer = new uint8_t[max(1,pw/4)*max(1,ph/4)*sizeof(uint8_t)*2+LZMA_PROPS_SIZE+4096];

				size_t bufferLen = LzmaSlowCompress((uint8_t*)imageData.pvrtc_d0, buffer, max(1,pw/4)*max(1,ph/4)*sizeof(uint8_t));

				write_uint24(bufferLen,ofile);

				ofile.write((const char *)buffer,bufferLen);
				outlzmasize += bufferLen;
				delete [] buffer;
			}
			
			{ // pvrtc d1
				uint8_t *buffer = new uint8_t[max(1,pw/4)*max(1,ph/4)*sizeof(uint32_t)*2+LZMA_PROPS_SIZE+4096];

				size_t bufferLen = LzmaSlowCompress((uint8_t*)imageData.pvrtc_d1, buffer, max(1,pw/4)*max(1,ph/4)*sizeof(uint32_t));

				write_uint24(bufferLen,ofile);

				ofile.write((const char *)buffer,bufferLen);
				outlzmasize += bufferLen;
				delete [] buffer;
			}

			jxr_container_t container = jxr_create_container();
			jxrc_start_file(container);

			if ( jxrc_begin_ifd_entry(container) != 0 ) {
				cerr << "Could not create ATF file!\n\n";
				return false;
			}
			jxrc_set_pixel_format(container, JXRC_FMT_16bppBGR555);
			jxrc_set_image_shape(container, max(1,pw/4), max(2,ph/2));
			jxrc_set_separate_alpha_image_plane(container, 0);
			jxrc_set_image_band_presence(container, JXR_BP_ALL);
			static unsigned char window_params[5] = {0,0,0,0,0};
			jxr_image_t image = jxr_create_image(max(1,pw/4), max(2,ph/2), window_params);

			if ( !image ) {
				return false;
			}

			SetJPEGX555(container,image,gJxrQuality, max(1,pw/4), max(2,ph/2));

			jxrc_begin_image_data(container);
			jxr_set_block_input(image, Read555Data_PVRTC);  
			jxr_set_user_data(image, &imageData);

			if ( jxr_write_image_bitstream(image,container) != 0 ) {
				cerr << "JPEGXR encoding error!\n";
				return false;
			}

			jxr_destroy(image); 

			jxrc_write_container_post(container);

			//write_debug_image(container);
			
			write_uint24(container->wb.len(),ofile);
			ofile.write((const char *)container->wb.buffer(),container->wb.len());
			
			jxr_destroy_container(container);
			
			delete [] imageData.pvrtc_col;
			delete [] imageData.pvrtc_d0;
			delete [] imageData.pvrtc_d1;
		}
	} else {
		if ( gStoreRawCompressed ) {
			write_uint24(0,ofile);
		} else {
			write_uint24(0,ofile);
			write_uint24(0,ofile);
			write_uint24(0,ofile);
		}
		for ( int32_t d=0; d<max(1,pw/4)*max(1,ph/4); d++) {
            read_uint32(ifile);
            read_uint32(ifile);
        }
	}
	return true;
}
				
static bool write_etc1(int32_t w, int32_t h, int32_t level, bool flipped, istream &ifile, ostream &ofile, bool alpha)
{
	if ( ( gCompressedFormats == 0 || gCompressedFormats == 2 ) && !(level < gEmbedRangeStart || level > gEmbedRangeEnd ) ) {

		if ( gStoreRawCompressed ) {

			uint32_t tsize = max(1,w/4)*max(1,h/4)*sizeof(uint32_t)*2;
            if ( alpha ) {
                tsize = max(1,w/4)*max(1,h/4)*sizeof(uint32_t)*4;
            }
			write_uint24(tsize,ofile);
			for ( int32_t d=0; d<tsize; d++) {
				write_uint8(read_uint8(ifile),ofile);
			}

		} else {

			ImageData imageData;
			imageData.flipped = flipped;
			imageData.etc1_col = new uint32_t[max(2,w/4)*max(2,h/2)*(alpha?2:1)];
			uint32_t *col = imageData.etc1_col;
			imageData.etc1_d0 = new uint8_t[max(1,w/4)*max(1,h/4)*(alpha?2:1)];
			uint8_t *d0 = (uint8_t *)imageData.etc1_d0;
			imageData.etc1_d1 = new uint32_t[max(1,w/4)*max(1,h/4)*(alpha?2:1)];
			uint8_t *d1 = (uint8_t *)imageData.etc1_d1;

			for ( int32_t d=0; d<max(1,w/4)*max(1,h/4)*(alpha?2:1); d++) {
				if ( gEncodeEmptyMipmap && level > 0 ) {
					*col++ = 0;
					*d0++ = 0;
					*d1++ = 0;
					*d1++ = 0;
					*d1++ = 0;
					*d1++ = 0;
				} else {
					*col++ = read_uint24(ifile);
					*d0++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
					*d1++ = read_uint8(ifile);
				}
			}

			{ // etc1 d0 data				
				uint8_t *buffer = new uint8_t[max(1,w/4)*max(1,h/4)*sizeof(uint8_t)*2*(alpha?2:1)+LZMA_PROPS_SIZE+4096];

				size_t bufferLen = LzmaSlowCompress((uint8_t*)imageData.etc1_d0, buffer, max(1,w/4)*max(1,h/4)*sizeof(uint8_t)*(alpha?2:1));

				write_uint24(bufferLen,ofile);

				ofile.write((const char *)buffer,bufferLen);
				outlzmasize += bufferLen;

				delete [] buffer;
			}

			{ // etc1 d1 data				
				uint8_t *buffer = new uint8_t[max(1,w/4)*max(1,h/4)*sizeof(uint32_t)*2*(alpha?2:1)+LZMA_PROPS_SIZE+4096];
				
				size_t bufferLen = LzmaSlowCompress((uint8_t*)imageData.etc1_d1, buffer, max(1,w/4)*max(1,h/4)*sizeof(uint32_t)*(alpha?2:1));

				write_uint24(bufferLen,ofile);

				ofile.write((const char *)buffer,bufferLen);
				outlzmasize += bufferLen;

				delete [] buffer;
			}

			jxr_container_t container = jxr_create_container();
			jxrc_start_file(container);
			
			if ( jxrc_begin_ifd_entry(container) != 0 ) {
				cerr << "Could not create ATF file!\n\n";
				return false;
			}
			jxrc_set_pixel_format(container, JXRC_FMT_16bppBGR555);
			jxrc_set_image_shape(container, max(1,w/4), max(2,h/2)*(alpha?2:1));
			jxrc_set_separate_alpha_image_plane(container, 0);
			jxrc_set_image_band_presence(container, JXR_BP_ALL);
			static unsigned char window_params[5] = {0,0,0,0,0};
			jxr_image_t image = jxr_create_image(max(1,w/4), max(2,h/2)*(alpha?2:1), window_params);

			if ( !image ) {
				return false;
			}

			SetJPEGX555(container,image,gJxrQuality, max(1,w/4), max(2,h/2)*(alpha?2:1));

			jxrc_begin_image_data(container);
			jxr_set_block_input(image, Read555Data_ETC1);  
			jxr_set_user_data(image, &imageData);

			if ( jxr_write_image_bitstream(image,container) != 0 ) {
				cerr << "JPEGXR encoding error!\n";
				return false;
			}

			jxr_destroy(image); 

			jxrc_write_container_post(container);

			//write_debug_image(container);

			write_uint24(container->wb.len(),ofile);
			ofile.write((const char *)container->wb.buffer(),container->wb.len());
			
			jxr_destroy_container(container);

			delete [] imageData.etc1_col;
			delete [] imageData.etc1_d0;
			delete [] imageData.etc1_d1;
		}
	} else {
		if ( gStoreRawCompressed ) {
			write_uint24(0,ofile);
		} else {
			write_uint24(0,ofile);
			write_uint24(0,ofile);
			write_uint24(0,ofile);
		}
		for ( int32_t d=0; d<max(1,w/4)*max(1,h/4)*(alpha?2:1); d++) {
            read_uint32(ifile);
            read_uint32(ifile);
        }
	}
	return true;
}

static bool write_raw_jxr(istream &ifile_raw, ostream &ofile) {
	if ( gJxrQualityDefault ) {
		gJxrQuality = 15;
	}
	
	if ( gJxrFormatDefault ) {
		gJxrFormat = JXR_YUV420;
	}
	
	if ( gTrimFlexBitsDefault ) {
		if ( gJxrQuality > 5 ) {
			gTrimFlexBits = 3;
		}
	}

	ifile_raw.seekg(0,ios_base::end);
	infilesize += ifile_raw.tellg();
	ifile_raw.seekg(0,ios_base::beg);

	PVR_HEADER pvr_header = { 0 };

	if (!read_pvr(ifile_raw,pvr_header)) {
		cerr << "Could not read pvr file!\n\n";
		return false;
	}

	if ( !validate_texture(pvr_header) ) {
		return false;
	}

	if ( ( pvr_header.dwpfFlags & 0xFF ) == PVR_OGL_RGBA_8888 ) {
		// trimming flex bits cause crashers during decode.
		gTrimFlexBits = 0;
	}

	if ( ( pvr_header.dwpfFlags & 0xFF ) != PVR_OGL_RGBA_8888 &&
		 ( pvr_header.dwpfFlags & 0xFF ) != PVR_OGL_RGB_888 ) {
		cerr << "Illegal raw texture type.\n\n(Hint 1: In PVRTexTool CL the type must be either 'OGL888' pr 'OGL8888')\n(Hint 2: In PVRTexTool select the 'OpenGL' tab in the 'Encode Texture:' dialog and select 'RGBA 8888' or 'RGB 888')\n\n";
		return false;
	}
	
	if ( pvr_header.dwWidth > 2048) {
		cerr << "Textures sizes are limited to 2048x2048!\n\n";
		return false;
	}

	if ( (pvr_header.dwWidth & (pvr_header.dwWidth - 1)) ||
		 (pvr_header.dwHeight & (pvr_header.dwHeight - 1)) ) {
		cerr << "Dimensions not a power of 2!\n\n";
		return false;
	}

	bool cubeMap = false;
	if ( pvr_header.dwpfFlags & PVRTEX_CUBEMAP) {
		cubeMap = true;
	}

	if ( ofile.bad() ) {
		return false;
	}

	int32_t w = texturew = pvr_header.dwWidth;
	int32_t h = texturew = pvr_header.dwHeight;
	
	if ( ( pvr_header.dwpfFlags & 0xFF ) == PVR_OGL_RGBA_8888 ) {
		texturecomp = 4;
		write_header(w,h,ATF_FORMAT_8888|(cubeMap?ATF_FORMAT_CUBEMAP:0),pvr_header.dwMipMapCount+1,ofile);
	} else {
		write_header(w,h,ATF_FORMAT_888 |(cubeMap?ATF_FORMAT_CUBEMAP:0),pvr_header.dwMipMapCount+1,ofile);
	}

    int32_t raw_pos = ifile_raw.tellg();
	
	for ( int32_t i=0; i<(cubeMap?6:1); i++) {

		w = texturew = pvr_header.dwWidth;
		h = texturew = pvr_header.dwHeight;

		if ( cubeMap ) {
            if ( pvr_header.dwpfFlags & ( PVRTEX_DDSCUBEMAPORDER | PVRTEX_PVRCUBEMAPORDER ) ) {
    			const int32_t dds2ogl[] = { 1, 0, 3, 2, 5, 4 };
                int32_t pos = raw_pos + pvr_header.dwTextureDataSize * dds2ogl[i];
	    		ifile_raw.seekg( ( pos ), ios_base::beg);
            }
		}
	
		for ( int32_t c=0; (c<pvr_header.dwMipMapCount+1) && (w>0||h>0); c++ ) {
		
            if ( c < gEmbedRangeStart || c > gEmbedRangeEnd ) {

			    write_uint24(0,ofile);
				int32_t l = max(1,w)*max(1,h)*3;
				for ( int32_t d=0; d<l; d++) {
                    read_uint8(ifile_raw);
                }

            } else {
			    ImageData imageData;
			    imageData.flipped = ( pvr_header.dwpfFlags & PVRTEX_FLIPPED ) ? true : false;

			    if ( ifile_raw.eof() ) {
				    cerr << "pvr file is short!\n\n";
				    return false;
			    }

			    imageData.raw = new uint8_t [max(1,w)*max(1,h)*4];
			    uint8_t *raw = imageData.raw;
			    if ( ( pvr_header.dwpfFlags & 0xFF ) == PVR_OGL_RGBA_8888 ) {
				    int32_t l = max(1,w)*max(1,h)*4;
				    for ( int32_t d=0; d<l; d++) {
					    if ( gEncodeEmptyMipmap && c > 0 ) {
						    *raw++ = 0;
					    } else {
						    *raw++ = read_uint8(ifile_raw);
					    }
				    }
			    } else {
				    int32_t l = max(1,w)*max(1,h)*3;
				    for ( int32_t d=0; d<l; d++) {
					    if ( gEncodeEmptyMipmap && c > 0 ) {
						    *raw++ = 0;
					    } else {
						    *raw++ = read_uint8(ifile_raw);
					    }
				    }
			    }

			    jxr_container_t container = jxr_create_container();
			    jxrc_start_file(container);

			    if ( jxrc_begin_ifd_entry(container) != 0 ) {
				    cerr << "Could not create ATF file!\n\n";
				    return false;
			    }

			    if ( ( pvr_header.dwpfFlags & 0xFF ) == PVR_OGL_RGBA_8888 ) {
				    jxrc_set_pixel_format(container, JXRC_FMT_32bppBGRA);
			    } else {
				    jxrc_set_pixel_format(container, JXRC_FMT_24bppBGR);
			    }
			
			    jxrc_set_image_shape(container, max(1,w), max(1,h));
			    jxrc_set_separate_alpha_image_plane(container, 0);
			    jxrc_set_image_band_presence(container, JXR_BP_ALL);

			    static unsigned char window_params[5] = {0,0,0,0,0};
			    jxr_image_t image = jxr_create_image(max(1,w), max(1,h), window_params);
		    
			    if ( !image ) {
				    cerr << "Could not create image!\n\n";
				    return false;
			    }
		    
			    SetJPEGXRaw(container,image,gJxrQuality,( pvr_header.dwpfFlags & 0xFF ) == PVR_OGL_RGBA_8888, max(1,w), max(1,h));

			    jxrc_begin_image_data(container);
			    if ( ( pvr_header.dwpfFlags & 0xFF ) == PVR_OGL_RGBA_8888 ) {
				    jxr_set_block_input(image, Read8888Data);  
			    } else {
				    jxr_set_block_input(image, Read888Data);  
			    }
			    jxr_set_user_data(image, &imageData);
			
			    if ( jxr_write_image_bitstream(image,container) != 0 ) {
				    cerr << "JPEGXR encoding error!\n";
				    return false;
			    }

			    jxr_destroy(image); 

			    jxrc_write_container_post(container);

			    write_uint24(container->wb.len(),ofile);
			    ofile.write((const char *)container->wb.buffer(),container->wb.len());
			
			    //write_debug_image(container);

			    jxr_destroy_container(container);
			
			    delete [] imageData.raw;
			    imageData.raw = 0;
            }
            			
			w /= 2;
			h /= 2;
		}
	}
	return true;
}

static bool write_compressed_alpha_textures(istream &ifile_etc1, istream &ifile_pvrtc, istream &ifile_dxt5, ostream &ofile) {

	if ( gJxrQualityDefault ) {
		gJxrQuality = 0;
	}
	
	if ( gJxrFormatDefault ) {
		gJxrFormat = JXR_YUV444;
	}
	
	if ( gTrimFlexBitsDefault ) {
		gTrimFlexBits = 0;
	}

    ifile_dxt5.seekg(0,ios_base::end);
	ifile_etc1.seekg(0,ios_base::end);
	ifile_pvrtc.seekg(0,ios_base::end);

	infilesize += ifile_dxt5.tellg();
	infilesize += ifile_etc1.tellg();
	infilesize += ifile_pvrtc.tellg();

	ifile_dxt5.seekg(0,ios_base::beg);
	ifile_etc1.seekg(0,ios_base::beg);
	ifile_pvrtc.seekg(0,ios_base::beg);

	PVR_HEADER * checkHeader = 0;
	
	// dxt5
	PVR_HEADER pvr_header_dxt5 = { 0 };
	if ( gCompressedFormats == 0 || gCompressedFormats == 1 ) {
		if (!read_pvr(ifile_dxt5,pvr_header_dxt5)) {
			cerr << "Could not read dxt5 pvr file!\n\n";
			return false;
		}

		if ( !validate_texture(pvr_header_dxt5) ) {
			return false;
		}

		if ( ( pvr_header_dxt5.dwpfFlags & 0xFF ) != PVR_D3D_DXT5 ) {
			cerr << "Illegal dxt5 texture type.\n\n(Hint 1: In PVRTexTool CL type needs to be 'DXT5')\n(Hint 2: In PVRTexTool UI select the 'DirectX 9' tab in the 'Encode Texture:' dialog and select 'DXT5')\n\n";
			return false;
		}
		checkHeader = &pvr_header_dxt5;
	}

	// etc1
	PVR_HEADER pvr_header_etc1 = { 0 };
	if ( gCompressedFormats == 0 || gCompressedFormats == 2 ) {
		if (!read_pvr(ifile_etc1,pvr_header_etc1)) {
			cerr << "Could not read etc1 pvr file!\n\n";
			return false;
		}
		
		if ( !validate_texture(pvr_header_etc1) ) {
			return false;
		}

		if ( ( pvr_header_etc1.dwpfFlags & 0xFF ) != PVR_ETC_RGB_4BPP ) {
			cerr << "Illegal etc1 texture type.\n\n(Hint 1: In PVRTexTool CL type needs to be 'ETC')\n(Hint 2: In PVRTexTool UI select the 'OpenGL ES2.0' tab in the 'Encode Texture:' dialog and select 'ETC')\n\n";
			return false;
		}
		checkHeader = &pvr_header_etc1;
	}
	
	// pvrtc
	PVR_HEADER pvr_header_pvrtc = { 0 };
	if ( gCompressedFormats == 0 || gCompressedFormats == 3 ) {
		if (!read_pvr(ifile_pvrtc,pvr_header_pvrtc)) {
			cerr << "Could not read pvrtc pvr file!\n\n";
			return false;
		}

		if ( !validate_texture(pvr_header_pvrtc) ) {
			return false;
		}

		if ( ( pvr_header_pvrtc.dwpfFlags & 0xFF ) != PVR_OGL_PVRTC4 ) {
			cerr << "Illegal pvrtc texture type.\n\n(Hint 1: In PVRTexTool CL type needs to be OGLPVRTC4)\n(Hint 2: In PVRTexTool UI select the 'OpenGL ES2.0' tab in the 'Encode Texture:' dialog and select 'PVRTC 4BPP')\n\n";
			return false;
		}

		checkHeader = &pvr_header_pvrtc;
	}

	// general checks
	
	if ( checkHeader->dwWidth > 2048) {
		cerr << "Textures sizes are limited to 2048x2048!\n\n";
		return false;
	}
	
	if ( (checkHeader->dwWidth & (checkHeader->dwWidth - 1)) ||
		 (checkHeader->dwHeight & (checkHeader->dwHeight - 1)) ) {
		cerr << "Dimensions not a power of 2!\n\n";
		return false;
	}

	if ( checkHeader->dwWidth == 2048 && checkHeader->dwMipMapCount == 0 ) {
		cerr << "Compressed 2048x2048 textures require at least one mip-map level! (for the purpose of dropping the top level image if the device is limited to 1024x1024)\n";
		return false;
	}

	bool cubeMap = false;
	if ( checkHeader->dwpfFlags & PVRTEX_CUBEMAP) {
		cubeMap = true;
	}
	
	if ( gCompressedFormats == 0 ) {

		if ( pvr_header_etc1.dwWidth != pvr_header_dxt5.dwWidth ||
			 pvr_header_etc1.dwWidth != pvr_header_pvrtc.dwWidth ||
			 pvr_header_etc1.dwHeight != pvr_header_dxt5.dwHeight ||
			 pvr_header_etc1.dwHeight != pvr_header_pvrtc.dwHeight ) {
			cerr << "Texture sizes do not match!\n\n";
			return false;
		}

		if ( ( pvr_header_etc1.dwMipMapCount != pvr_header_dxt5.dwMipMapCount ) ||
			 pvr_header_etc1.dwMipMapCount != pvr_header_pvrtc.dwMipMapCount ) {
			cerr << "Mip map counts do not match!\n\n";
			return false;
		}
		
		if ( cubeMap ) {
			if ( ( pvr_header_etc1.dwpfFlags & PVRTEX_CUBEMAP ) == 0 ||
				 ( pvr_header_pvrtc.dwpfFlags & PVRTEX_CUBEMAP ) == 0 ||
				 ( pvr_header_dxt5.dwpfFlags & PVRTEX_CUBEMAP ) == 0) {
				cerr << "Texture types (cube maps) do not match!\n\n";
				return false;
			}
		}
	}

	int32_t w = texturew = checkHeader->dwWidth;
	int32_t h = textureh = checkHeader->dwHeight;
	
	write_header(w,h,(gStoreRawCompressed?ATF_FORMAT_COMPRESSEDRAWALPHA:ATF_FORMAT_COMPRESSEDALPHA)|(cubeMap?ATF_FORMAT_CUBEMAP:0),checkHeader->dwMipMapCount+1,ofile);
	
	size_t dxt5_pos = ifile_dxt5.tellg();
	size_t etc1_pos = ifile_etc1.tellg();
	size_t pvrtc_pos = ifile_pvrtc.tellg();

	for ( int32_t i=0; i<(cubeMap?6:1); i++) {

		if ( gCompressedFormats == 0 || gCompressedFormats == 1 ) {
			if ( cubeMap ) {
                if ( pvr_header_dxt5.dwpfFlags & ( PVRTEX_DDSCUBEMAPORDER | PVRTEX_PVRCUBEMAPORDER ) ) {
    				const int32_t dds2ogl[] = { 1, 0, 3, 2, 5, 4 };
                    int32_t pos = dxt5_pos + pvr_header_dxt5.dwTextureDataSize * dds2ogl[i];
	    			ifile_dxt5.seekg( ( pos ), ios_base::beg);
                } else {
    				const int32_t pvr2ogl[] = { 2, 3, 5, 4, 0, 1 };
                    int32_t pos = dxt5_pos + pvr_header_dxt5.dwTextureDataSize * pvr2ogl[i];
	    			ifile_dxt5.seekg( ( pos ), ios_base::beg);
                }
			}
		}

		if ( gCompressedFormats == 0 || gCompressedFormats == 2 ) {
			if ( cubeMap ) {
                if ( pvr_header_etc1.dwpfFlags & ( PVRTEX_DDSCUBEMAPORDER | PVRTEX_PVRCUBEMAPORDER ) ) {
    				const int32_t dds2ogl[] = { 1, 0, 3, 2, 5, 4 };
                    int32_t pos = etc1_pos + pvr_header_etc1.dwTextureDataSize * dds2ogl[i];
	    			ifile_etc1.seekg( ( pos ), ios_base::beg);
                }
			}
		}

		if ( gCompressedFormats == 0 || gCompressedFormats == 3 ) {
			if ( cubeMap ) {
                if ( pvr_header_pvrtc.dwpfFlags & ( PVRTEX_DDSCUBEMAPORDER | PVRTEX_PVRCUBEMAPORDER ) ) {
    				const int32_t dds2ogl[] = { 1, 0, 3, 2, 5, 4 };
                    int32_t pos = pvrtc_pos + pvr_header_pvrtc.dwTextureDataSize * dds2ogl[i];
	    			ifile_pvrtc.seekg( ( pos ), ios_base::beg);
                }
			}
		}

		w = texturew = checkHeader->dwWidth;
		h = texturew = checkHeader->dwHeight;
	
		for ( int32_t c=0; (c<checkHeader->dwMipMapCount+1) && (w>0||h>0); c++ ) {

			bool dxt_flipped = false;
			if ( gCompressedFormats == 0 || gCompressedFormats == 1 ) {
				dxt_flipped = ( pvr_header_dxt5.dwpfFlags & PVRTEX_FLIPPED ) ? true : false;
			}
			bool pvrtc_flipped = false;
			if ( gCompressedFormats == 0 || gCompressedFormats == 2 ) {
				pvrtc_flipped = ( pvr_header_etc1.dwpfFlags & PVRTEX_FLIPPED ) ? true : false;
			}
			bool etc1_flipped = false;
			if ( gCompressedFormats == 0 || gCompressedFormats == 3 ) {
				etc1_flipped = ( pvr_header_pvrtc.dwpfFlags & PVRTEX_FLIPPED ) ? true : false;
			}

			if ( !write_dxt5(w,h,c,dxt_flipped,ifile_dxt5,ofile) ) return false;
			if ( !write_pvrtc_alpha(w,h,c,pvrtc_flipped,ifile_pvrtc,ofile) ) return false;
			if ( !write_etc1(w,h,c,etc1_flipped,ifile_etc1,ofile,true) ) return false;

			w /= 2;
			h /= 2;
		}		
	}
	return true;
}

static bool write_compressed_textures(istream &ifile_etc1, istream &ifile_pvrtc, istream &ifile_dxt1, ostream &ofile) {
	if ( gJxrQualityDefault ) {
		gJxrQuality = 0;
	}
	
	if ( gJxrFormatDefault ) {
		gJxrFormat = JXR_YUV444;
	}
	
	if ( gTrimFlexBitsDefault ) {
		gTrimFlexBits = 0;
	}

	ifile_dxt1.seekg(0,ios_base::end);
	ifile_etc1.seekg(0,ios_base::end);
	ifile_pvrtc.seekg(0,ios_base::end);

	infilesize += ifile_dxt1.tellg();
	infilesize += ifile_etc1.tellg();
	infilesize += ifile_pvrtc.tellg();

	ifile_dxt1.seekg(0,ios_base::beg);
	ifile_etc1.seekg(0,ios_base::beg);
	ifile_pvrtc.seekg(0,ios_base::beg);

	PVR_HEADER * checkHeader = 0;
	
	// etc1
	PVR_HEADER pvr_header_etc1 = { 0 };
	if ( gCompressedFormats == 0 || gCompressedFormats == 2 ) {
		if (!read_pvr(ifile_etc1,pvr_header_etc1)) {
			cerr << "Could not read etc1 pvr file!\n\n";
			return false;
		}
		
		if ( !validate_texture(pvr_header_etc1) ) {
			return false;
		}

		if ( ( pvr_header_etc1.dwpfFlags & 0xFF ) != PVR_ETC_RGB_4BPP ) {
			cerr << "Illegal etc1 texture type.\n\n(Hint 1: In PVRTexTool CL type needs to be 'ETC')\n(Hint 2: In PVRTexTool UI select the 'OpenGL ES2.0' tab in the 'Encode Texture:' dialog and select 'ETC')\n\n";
			return false;
		}
		checkHeader = &pvr_header_etc1;
	}
	
	// dxt1
	PVR_HEADER pvr_header_dxt1 = { 0 };
	if ( gCompressedFormats == 0 || gCompressedFormats == 1 ) {
		if (!read_pvr(ifile_dxt1,pvr_header_dxt1)) {
			cerr << "Could not read dxt1 pvr file!\n\n";
			return false;
		}

		if ( !validate_texture(pvr_header_dxt1) ) {
			return false;
		}

		if ( ( pvr_header_dxt1.dwpfFlags & 0xFF ) != PVR_D3D_DXT1 ) {
			cerr << "Illegal dxt1 texture type.\n\n(Hint 1: In PVRTexTool CL type needs to be 'DXT1')\n(Hint 2: In PVRTexTool UI select the 'DirectX 9' tab in the 'Encode Texture:' dialog and select 'DXT1')\n\n";
			return false;
		}
		checkHeader = &pvr_header_dxt1;
	}
	
	// pvrtc
	PVR_HEADER pvr_header_pvrtc = { 0 };
	if ( gCompressedFormats == 0 || gCompressedFormats == 3 ) {
		if (!read_pvr(ifile_pvrtc,pvr_header_pvrtc)) {
			cerr << "Could not read pvrtc pvr file!\n\n";
			return false;
		}

		if ( !validate_texture(pvr_header_pvrtc) ) {
			return false;
		}

		if ( ( pvr_header_pvrtc.dwpfFlags & 0xFF ) != PVR_OGL_PVRTC4 ) {
			cerr << "Illegal pvrtc texture type.\n\n(Hint 1: In PVRTexTool CL type needs to be OGLPVRTC4)\n(Hint 2: In PVRTexTool UI select the 'OpenGL ES2.0' tab in the 'Encode Texture:' dialog and select 'PVRTC 4BPP')\n\n";
			return false;
		}

		checkHeader = &pvr_header_pvrtc;
	}

	// general checks
	
	if ( checkHeader->dwWidth > 2048) {
		cerr << "Textures sizes are limited to 2048x2048!\n\n";
		return false;
	}
	
	if ( (checkHeader->dwWidth & (checkHeader->dwWidth - 1)) ||
		 (checkHeader->dwHeight & (checkHeader->dwHeight - 1)) ) {
		cerr << "Dimensions not a power of 2!\n\n";
		return false;
	}

	if ( checkHeader->dwWidth == 2048 && checkHeader->dwMipMapCount == 0 ) {
		cerr << "Compressed 2048x2048 textures require at least one mip-map level! (for the purpose of dropping the top level image if the device is limited to 1024x1024)\n";
		return false;
	}

	bool cubeMap = false;
	if ( checkHeader->dwpfFlags & PVRTEX_CUBEMAP) {
		cubeMap = true;
	}
	
	if ( gCompressedFormats == 0 ) {
		if ( ( pvr_header_etc1.dwWidth != pvr_header_dxt1.dwWidth ) ||
			 pvr_header_etc1.dwWidth != pvr_header_pvrtc.dwWidth ||
			 ( pvr_header_etc1.dwHeight != pvr_header_dxt1.dwHeight) ||
			 pvr_header_etc1.dwHeight != pvr_header_pvrtc.dwHeight ) {
			cerr << "Texture sizes do not match!\n\n";
			return false;
		}

		if ( ( pvr_header_etc1.dwMipMapCount != pvr_header_dxt1.dwMipMapCount ) ||
			 pvr_header_etc1.dwMipMapCount != pvr_header_pvrtc.dwMipMapCount ) {
			cerr << "Mip map counts do not match!\n\n";
			return false;
		}
		
		if ( cubeMap ) {
			if ( ( pvr_header_etc1.dwpfFlags & PVRTEX_CUBEMAP ) == 0 ||
				 ( pvr_header_pvrtc.dwpfFlags & PVRTEX_CUBEMAP ) == 0 ||
				 ( pvr_header_dxt1.dwpfFlags & PVRTEX_CUBEMAP ) == 0 ) {
				cerr << "Texture types (cube maps) do not match!\n\n";
				return false;
			}
		}
	}

	int32_t w = texturew = checkHeader->dwWidth;
	int32_t h = textureh = checkHeader->dwHeight;
	
	write_header(w,h,(gStoreRawCompressed ? ATF_FORMAT_COMPRESSEDRAW : ATF_FORMAT_COMPRESSED )|(cubeMap?ATF_FORMAT_CUBEMAP:0),checkHeader->dwMipMapCount+1,ofile);
	
	size_t dxt1_pos = ifile_dxt1.tellg();
	size_t etc1_pos = ifile_dxt1.tellg();
	size_t pvrtc_pos = ifile_dxt1.tellg();

	for ( int32_t i=0; i<(cubeMap?6:1); i++) {

		if ( gCompressedFormats == 0 || gCompressedFormats == 1 ) {
			if ( cubeMap ) {
                if ( pvr_header_dxt1.dwpfFlags & ( PVRTEX_DDSCUBEMAPORDER | PVRTEX_PVRCUBEMAPORDER ) ) {
    				const int32_t dds2ogl[] = { 1, 0, 3, 2, 5, 4 };
                    int32_t pos = dxt1_pos + pvr_header_dxt1.dwTextureDataSize * dds2ogl[i];
	    			ifile_dxt1.seekg( ( pos ), ios_base::beg);
                } else {
    				const int32_t pvr2ogl[] = { 2, 3, 5, 4, 0, 1 };
                    int32_t pos = dxt1_pos + pvr_header_dxt1.dwTextureDataSize * pvr2ogl[i];
	    			ifile_dxt1.seekg( ( pos ), ios_base::beg);
                }
			}
		}

		if ( gCompressedFormats == 0 || gCompressedFormats == 2 ) {
			if ( cubeMap ) {
                if ( pvr_header_etc1.dwpfFlags & ( PVRTEX_DDSCUBEMAPORDER | PVRTEX_PVRCUBEMAPORDER ) ) {
    				const int32_t dds2ogl[] = { 1, 0, 3, 2, 5, 4 };
                    int32_t pos = etc1_pos + pvr_header_etc1.dwTextureDataSize * dds2ogl[i];
	    			ifile_etc1.seekg( ( pos ), ios_base::beg);
                }
			}
		}

		if ( gCompressedFormats == 0 || gCompressedFormats == 3 ) {
			if ( cubeMap ) {
                if ( pvr_header_pvrtc.dwpfFlags & ( PVRTEX_DDSCUBEMAPORDER | PVRTEX_PVRCUBEMAPORDER ) ) {
    				const int32_t dds2ogl[] = { 1, 0, 3, 2, 5, 4 };
                    int32_t pos = pvrtc_pos + pvr_header_pvrtc.dwTextureDataSize * dds2ogl[i];
	    			ifile_pvrtc.seekg( ( pos ), ios_base::beg);
                }
			}
		}

		w = texturew = checkHeader->dwWidth;
		h = texturew = checkHeader->dwHeight;
	
		for ( int32_t c=0; (c<checkHeader->dwMipMapCount+1) && (w>0||h>0); c++ ) {

			bool dxt_flipped = false;
			if ( gCompressedFormats == 0 || gCompressedFormats == 1 ) {
				dxt_flipped = ( pvr_header_dxt1.dwpfFlags & PVRTEX_FLIPPED ) ? true : false;
			}
			bool pvrtc_flipped = false;
			if ( gCompressedFormats == 0 || gCompressedFormats == 2 ) {
				pvrtc_flipped = ( pvr_header_etc1.dwpfFlags & PVRTEX_FLIPPED ) ? true : false;
			}
			bool etc1_flipped = false;
			if ( gCompressedFormats == 0 || gCompressedFormats == 3 ) {
				etc1_flipped = ( pvr_header_pvrtc.dwpfFlags & PVRTEX_FLIPPED ) ? true : false;
			}

			if ( !write_dxt1(w,h,c,dxt_flipped,ifile_dxt1,ofile) ) return false;
			if ( !write_pvrtc(w,h,c,pvrtc_flipped,ifile_pvrtc,ofile) ) return false;
			if ( !write_etc1(w,h,c,etc1_flipped,ifile_etc1,ofile,false) ) return false;

			w /= 2;
			h /= 2;
		}		
	}
	return true;
}

bool convert_with_alpha(istream &ifile_etc1, istream &ifile_pvrtc, istream &ifile_dxt5, ostream &ofile) {
	if ( !write_compressed_alpha_textures(ifile_etc1,ifile_pvrtc,ifile_dxt5,ofile) ) {
		return false;
	}

	size_t filesize = ofile.tellp();
	filesize -= 6;
	ofile.seekp(3);

	ofile.put(uint8_t((filesize>>16)&0xFF));
	ofile.put(uint8_t((filesize>> 8)&0xFF));
	ofile.put(uint8_t((filesize>> 0)&0xFF));
	
	ofile.seekp(0,ios_base::end);

    return true;
}

bool convert(istream &ifile_etc1, istream &ifile_pvrtc, istream &ifile_dxt1, istream &ifile_raw, ostream &ofile ) {
	
	if ( gEncodeRawJXR ) {
		if ( !write_raw_jxr(ifile_raw,ofile) ) {
			return false;
		}
	} else {
		if ( !write_compressed_textures(ifile_etc1,ifile_pvrtc,ifile_dxt1,ofile) ) {
			return false;
		}
	}
	
	size_t filesize = ofile.tellp();
	filesize -= 6;
	ofile.seekp(3);

	ofile.put(uint8_t((filesize>>16)&0xFF));
	ofile.put(uint8_t((filesize>> 8)&0xFF));
	ofile.put(uint8_t((filesize>> 0)&0xFF));
	
	ofile.seekp(0,ios_base::end);

	return true;
}
