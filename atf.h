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

#ifndef _ATF_H_
#define _ATF_H_

//
// ATF format:
//
// U8[3]   -  signature  - 'ATF'
// U8      -  format     - 0=RGB888, 1=RGBA8888 (straight alpha), 2=Compressed(dxt1+pvrtc+etc1)
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

enum {
// uncompressed formats (dwpfFlags&0xFF)
	PVR_OGL_RGBA_8888		= 0x12,
	PVR_OGL_RGB_888			= 0x15,
// compressed formats (dwpfFlags&0xFF)
	PVR_OGL_PVRTC4			= 0x19,
	PVR_D3D_DXT1			= 0x20,
	PVR_D3D_DXT5			= 0x24,
	PVR_ETC_RGB_4BPP		= 0x36,
// flags (dwpfFlags)
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
	uint8_t			dwPVR[4];
	uint32_t		dwNumSurfs;
};

class ATFDecoder {

	public:
	
		enum {
			PREFER_DXT1,
			PREFER_PVRTC,
			PREFER_ETC1,
			PREFER_FALLBACK
		};

		ATFDecoder(const uint8_t *data, size_t dataLen, bool viewerMode = false);

		bool decode(int32_t preferredFormat);
		
		const PVR_HEADER *tex() { return (PVR_HEADER *)m_tex; }
		size_t texLen() { return m_texLen; }

		uint8_t *texData(uint32_t level, uint32_t side = 0);
		
		bool LevelAvailable(int32_t level) const { return !m_levelEmpty[level]; }
		bool IsEmpty() const { for ( int32_t c=0; c<m_count; c++) { if (!m_levelEmpty[c]) return false; } return true; }

		enum {
			ATF_FORMAT_888				 = 0x00,
			ATF_FORMAT_8888				 = 0x01,
			ATF_FORMAT_COMPRESSED		 = 0x02,
			ATF_FORMAT_COMPRESSEDRAW	 = 0x03,
			ATF_FORMAT_COMPRESSEDALPHA	 = 0x04,
			ATF_FORMAT_COMPRESSEDRAWALPHA= 0x05,
			ATF_FORMAT_LAST				 = 0x05,
			ATF_FORMAT_CUBEMAP			 = 0x80,
		};

        int32_t Format() const { return m_format; }

	private:
	
		static void pack_image(jxr_image_t image, int mx, int my, int *src);

	private:
		bool read_image(size_t len, jxrc_t_pixelFormat format, int32_t w, int32_t h);
		
		bool init_pvr_header();
		
		bool lzma_decode_top(size_t len, int32_t w, int32_t h, bool left);
		bool lzma_decode_bottom(size_t len, int32_t w, int32_t h, bool left);
		bool lzma_decode_wide(size_t len, int32_t w, int32_t h, bool left);
		
		bool convert_888_texture(int32_t w, int32_t h, bool &empty);
		bool convert_8888_texture(int32_t w, int32_t h, bool &empty);
		bool convert_dxt1_texture(bool skip, int32_t w, int32_t h, bool &empty);
		bool convert_dxt5_texture(bool skip, int32_t w, int32_t h, bool &empty);
		bool convert_pvrtc_alpha_texture(bool skip, int32_t w, int32_t h, bool &empty);
		bool convert_pvrtc_texture(bool skip, int32_t w, int32_t h, bool &empty);
		bool convert_etc1_texture(bool skip, int32_t w, int32_t h, bool &empty);
		bool convert_dxt1_raw_texture(bool skip, int32_t w, int32_t h, bool &empty);
		bool convert_pvrtc_raw_texture(bool skip, int32_t w, int32_t h, bool &empty);
		bool convert_etc1_raw_texture(bool skip, int32_t w, int32_t h, bool &empty);
		bool convert_dxt5_raw_texture(bool skip, int32_t w, int32_t h, bool &empty);

		bool check_buffer_read(size_t toRead) {
			if ((m_src-m_data+toRead)<=m_dataLen) {
				return true;
			}
			return false;
		}

		uint8_t get_u8() {
			if ((m_src-m_data)<m_dataLen) {
				return *m_src++;
			}
			return 0;
		}

		uint32_t get_u24() {	
			if ((m_src+3-m_data)<m_dataLen) {
				uint32_t v = ((m_src[0])<<16)|
							 ((m_src[1])<< 8)|
							 ((m_src[2])<< 0);
				m_src += 3;
				return v;
			}
			return 0;
		}

		static inline int32_t pvrtc_twiddle(int32_t u, int32_t v, int32_t w, int32_t h)
		{
			int32_t r = 0;
			int32_t mins;
			int32_t maxv;
			if(h < w) {
				mins = h;
				maxv = u;
			} else {
				mins = w;
				maxv = v;
			}
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
			return r|(maxv<<(2*c));
		}

		bool			m_alpha;
		int32_t			m_mode;
		size_t			m_texLen;
		uint8_t	*		m_tex;	
		int32_t			m_format;
		int32_t			m_count;
		int32_t			m_width;
		int32_t			m_height;
		const uint8_t *	m_src;
		uint8_t *		m_dst;
		uint8_t *		m_tmp;
		const uint8_t *	m_data;
		size_t			m_dataLen;
		size_t			m_fileLen;
        bool            m_viewerMode;
        bool			m_levelEmpty[16];
};

#endif //#ifndef _ATF_H_
