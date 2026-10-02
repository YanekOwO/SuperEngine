/*
    pygame - Python Game Library
    Copyright (C) 2000-2001  Pete Shinners
    Copyright (C) 2006  Rene Dudfield

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

    Pete Shinners
    pete@shinners.org
*/

#include <SDL3/SDL.h>


/* The structure passed to the low level blit functions */
typedef struct {
        Uint8 *s_pixels;
        int s_width;
        int s_height;
        int s_skip;
        Uint8 *d_pixels;
        int d_width;
        int d_height;
        int d_skip;
        void *aux_data;
        const SDL_PixelFormatDetails *src;
        Uint8 *table;
        const SDL_PixelFormatDetails *dst;
        Uint8 src_alpha;
        Uint32 src_colorkey;
} SDL_BlitInfo;
static void alphablit_alpha(SDL_BlitInfo *info);
static void alphablit_colorkey(SDL_BlitInfo *info);
static void alphablit_solid(SDL_BlitInfo *info);



static int SoftBlitPyGame(SDL_Surface *src, SDL_Rect *srcrect,
                          SDL_Surface *dst, SDL_Rect *dstrect, int the_args);




static int SoftBlitPyGame(SDL_Surface *src, SDL_Rect *srcrect,
                        SDL_Surface *dst, SDL_Rect *dstrect, int the_args)
{
        int okay;
        int src_locked;
        int dst_locked;
        int src_bpp = SDL_BYTESPERPIXEL(src->format);
        int dst_bpp = SDL_BYTESPERPIXEL(dst->format);

    /* Everything is okay at the beginning...  */
        okay = 1;

        /* Lock the destination if it's in hardware */
        dst_locked = 0;
        if ( SDL_MUSTLOCK(dst) ) {
                if ( !SDL_LockSurface(dst) )
                        okay = 0;
                else
                        dst_locked = 1;
        }
        /* Lock the source if it's in hardware */
        src_locked = 0;
        if ( SDL_MUSTLOCK(src) ) {
                if ( !SDL_LockSurface(src) )
                        okay = 0;
                else
                        src_locked = 1;
        }

        /* Set up source and destination buffer pointers, and BLIT! */
        if ( okay  && srcrect->w && srcrect->h ) {
                SDL_BlitInfo info;

                /* Set up the blit information */
                info.s_pixels = (Uint8 *)src->pixels +
                                (Uint16)srcrect->y*src->pitch +
                                (Uint16)srcrect->x*src_bpp;
                info.s_width = srcrect->w;
                info.s_height = srcrect->h;
                info.s_skip=src->pitch-info.s_width*src_bpp;
                info.d_pixels = (Uint8 *)dst->pixels +
                                (Uint16)dstrect->y*dst->pitch +
                                (Uint16)dstrect->x*dst_bpp;
                info.d_width = dstrect->w;
                info.d_height = dstrect->h;
                info.d_skip=dst->pitch-info.d_width*dst_bpp;
                info.src = SDL_GetPixelFormatDetails(src->format);
                info.dst = SDL_GetPixelFormatDetails(dst->format);
                info.src_alpha = 255;
                SDL_GetSurfaceAlphaMod(src, &info.src_alpha);
                info.src_colorkey = 0;

                if (the_args != 0) {
                        SDL_SetError("Invalid argument passed to blit.");
                        okay = 0;
                } else {
                        SDL_BlendMode blend_mode = SDL_BLENDMODE_NONE;
                        SDL_GetSurfaceBlendMode(src, &blend_mode);
                        if (blend_mode != SDL_BLENDMODE_NONE &&
                            SDL_ISPIXELFORMAT_ALPHA(src->format))
                                alphablit_alpha(&info);
                        else if (SDL_GetSurfaceColorKey(src,
                                                        &info.src_colorkey))
                                alphablit_colorkey(&info);
                        else
                                alphablit_solid(&info);
                }

        }

        /* We need to unlock the surfaces if they're locked */
        if ( dst_locked )
                SDL_UnlockSurface(dst);
        if ( src_locked )
                SDL_UnlockSurface(src);
        /* Blit is done! */
        return(okay ? 0 : -1);
}



/*
 * Macros below are for the blit blending functions.
 */


#define DISEMBLE_RGBA(buf, bpp, fmt, pixel, R, G, B, A)                    \
do {                                                                       \
        switch (bpp) {                                                           \
                case 2:                                                           \
                        pixel = *((Uint16 *)(buf));                           \
                break;                                                           \
                case 4:                                                           \
                        pixel = *((Uint32 *)(buf));                           \
                break;                                                           \
                default:        {/* case 3: FIXME: broken code (no alpha) */                   \
                        Uint8 *b = (Uint8 *)buf;                           \
                        if(SDL_BYTEORDER == SDL_LIL_ENDIAN) {                   \
                                pixel = b[0] + (b[1] << 8) + (b[2] << 16); \
                        } else {                                           \
                                pixel = (b[0] << 16) + (b[1] << 8) + b[2]; \
                        }                                                   \
                }                                                           \
                break;                                                           \
        }                                                                   \
        R = ((pixel&fmt->Rmask)>>fmt->Rshift)<<(8-fmt->Rbits);             \
        G = ((pixel&fmt->Gmask)>>fmt->Gshift)<<(8-fmt->Gbits);             \
        B = ((pixel&fmt->Bmask)>>fmt->Bshift)<<(8-fmt->Bbits);             \
        A = ((pixel&fmt->Amask)>>fmt->Ashift)<<(8-fmt->Abits);             \
} while(0)


#define PIXEL_FROM_RGBA(pixel, fmt, r, g, b, a)                         \
{                                                                       \
        pixel = ((r>>(8-fmt->Rbits))<<fmt->Rshift)|                            \
                ((g>>(8-fmt->Gbits))<<fmt->Gshift)|                            \
                ((b>>(8-fmt->Bbits))<<fmt->Bshift)|                            \
                ((a<<(8-fmt->Abits))<<fmt->Ashift);                            \
}
#define ASSEMBLE_RGBA(buf, bpp, fmt, r, g, b, a)                        \
{                                                                       \
        switch (bpp) {                                                        \
                case 2: {                                                \
                        Uint16 pixel;                                        \
                        PIXEL_FROM_RGBA(pixel, fmt, r, g, b, a);        \
                        *((Uint16 *)(buf)) = pixel;                        \
                }                                                        \
                break;                                                        \
                case 4: {                                                \
                        Uint32 pixel;                                        \
                        PIXEL_FROM_RGBA(pixel, fmt, r, g, b, a);        \
                        *((Uint32 *)(buf)) = pixel;                        \
                }                                                        \
                break;                                                        \
        }                                                                \
}


#define ALPHA_BLEND(sR, sG, sB, sA, dR, dG, dB, dA)  \
do {   if(dA){\
        dR = ((dR<<8) + (sR-dR)*sA + sR) >> 8;	   \
        dG = ((dG<<8) + (sG-dG)*sA + sG) >> 8;     \
        dB = ((dB<<8) + (sB-dB)*sA + sB) >> 8;	   \
        dA = sA+dA - ((sA*dA)/255);                \
    }else{\
        dR = sR;                \
        dG = sG;                \
        dB = sB;                \
        dA = sA;               \
    }\
} while(0)




static void alphablit_alpha(SDL_BlitInfo *info)
{
        int n;
        int width = info->d_width;
        int height = info->d_height;
        Uint8 *src = info->s_pixels;
        int srcskip = info->s_skip;
        Uint8 *dst = info->d_pixels;
        int dstskip = info->d_skip;
        const SDL_PixelFormatDetails *srcfmt = info->src;
        const SDL_PixelFormatDetails *dstfmt = info->dst;
        int srcbpp = srcfmt->bytes_per_pixel;
        int dstbpp = dstfmt->bytes_per_pixel;
        int dR, dG, dB, dA, sR, sG, sB, sA;

        while ( height-- )
        {
            for(n=width; n>0; --n)
            {
                Uint32 pixel;
                DISEMBLE_RGBA(src, srcbpp, srcfmt, pixel, sR, sG, sB, sA);
                DISEMBLE_RGBA(dst, dstbpp, dstfmt, pixel, dR, dG, dB, dA);
                ALPHA_BLEND(sR, sG, sB, sA, dR, dG, dB, dA);
                ASSEMBLE_RGBA(dst, dstbpp, dstfmt, dR, dG, dB, dA);
                src += srcbpp;
                dst += dstbpp;
            }
            src += srcskip;
            dst += dstskip;
        }
}

static void alphablit_colorkey(SDL_BlitInfo *info)
{
        int n;
        int width = info->d_width;
        int height = info->d_height;
        Uint8 *src = info->s_pixels;
        int srcskip = info->s_skip;
        Uint8 *dst = info->d_pixels;
        int dstskip = info->d_skip;
        const SDL_PixelFormatDetails *srcfmt = info->src;
        const SDL_PixelFormatDetails *dstfmt = info->dst;
        int srcbpp = srcfmt->bytes_per_pixel;
        int dstbpp = dstfmt->bytes_per_pixel;
        int dR, dG, dB, dA, sR, sG, sB, sA;
        int alpha = info->src_alpha;
        Uint32 colorkey = info->src_colorkey;

        while ( height-- )
        {
            for(n=width; n>0; --n)
            {
                Uint32 pixel;
                DISEMBLE_RGBA(dst, dstbpp, dstfmt, pixel, dR, dG, dB, dA);
                DISEMBLE_RGBA(src, srcbpp, srcfmt, pixel, sR, sG, sB, sA);
                sA = (pixel == colorkey) ? 0 : alpha;
                ALPHA_BLEND(sR, sG, sB, sA, dR, dG, dB, dA);
                ASSEMBLE_RGBA(dst, dstbpp, dstfmt, dR, dG, dB, dA);
                src += srcbpp;
                dst += dstbpp;
            }
            src += srcskip;
            dst += dstskip;
        }
}


static void alphablit_solid(SDL_BlitInfo *info)
{
        int n;
        int width = info->d_width;
        int height = info->d_height;
        Uint8 *src = info->s_pixels;
        int srcskip = info->s_skip;
        Uint8 *dst = info->d_pixels;
        int dstskip = info->d_skip;
        const SDL_PixelFormatDetails *srcfmt = info->src;
        const SDL_PixelFormatDetails *dstfmt = info->dst;
        int srcbpp = srcfmt->bytes_per_pixel;
        int dstbpp = dstfmt->bytes_per_pixel;
        int dR, dG, dB, dA, sR, sG, sB, sA;
        int alpha = info->src_alpha;

        while ( height-- )
        {
            for(n=width; n>0; --n)
            {
                int pixel;
                DISEMBLE_RGBA(dst, dstbpp, dstfmt, pixel, dR, dG, dB, dA);
                DISEMBLE_RGBA(src, srcbpp, srcfmt, pixel, sR, sG, sB, sA);
                ALPHA_BLEND(sR, sG, sB, alpha, dR, dG, dB, dA);
                ASSEMBLE_RGBA(dst, dstbpp, dstfmt, dR, dG, dB, dA);
                src += srcbpp;
                dst += dstbpp;
            }
            src += srcskip;
            dst += dstskip;
        }
}



/*we assume the "dst" has pixel alpha*/
int pygame_Blit(SDL_Surface *src, SDL_Rect *srcrect,
                   SDL_Surface *dst, SDL_Rect *dstrect, int the_args)
{
        SDL_Rect fulldst;
        int srcx, srcy, w, h;

        if ( ! src || ! dst ) {
                SDL_SetError("%s", "SDL_UpperBlit: passed a NULL surface");
                return(-1);
        }

        /* If the destination rectangle is NULL, use the entire dest surface */
        if ( dstrect == NULL ) {
                fulldst.x = fulldst.y = 0;
                dstrect = &fulldst;
        }

        /* clip the source rectangle to the source surface */
        if(srcrect) {
                int maxw, maxh;

                srcx = srcrect->x;
                w = srcrect->w;
                if(srcx < 0) {
                        w += srcx;
                        dstrect->x -= srcx;
                        srcx = 0;
                }
                maxw = src->w - srcx;
                if(maxw < w)
                        w = maxw;

                srcy = srcrect->y;
                h = srcrect->h;
                if(srcy < 0) {
                        h += srcy;
                        dstrect->y -= srcy;
                        srcy = 0;
                }
                maxh = src->h - srcy;
                if(maxh < h)
                        h = maxh;

        } else {
                srcx = srcy = 0;
                w = src->w;
                h = src->h;
        }

        /* clip the destination rectangle against the clip rectangle */
        {
                SDL_Rect clip_rect;
                SDL_Rect *clip = &clip_rect;
                int dx, dy;

                SDL_GetSurfaceClipRect(dst, &clip_rect);

                dx = clip->x - dstrect->x;
                if(dx > 0) {
                        w -= dx;
                        dstrect->x += dx;
                        srcx += dx;
                }
                dx = dstrect->x + w - clip->x - clip->w;
                if(dx > 0)
                        w -= dx;

                dy = clip->y - dstrect->y;
                if(dy > 0) {
                        h -= dy;
                        dstrect->y += dy;
                        srcy += dy;
                }
                dy = dstrect->y + h - clip->y - clip->h;
                if(dy > 0)
                        h -= dy;
        }

        if(w > 0 && h > 0) {
                SDL_Rect sr;
                sr.x = srcx;
                sr.y = srcy;
                sr.w = dstrect->w = w;
                sr.h = dstrect->h = h;
                return SoftBlitPyGame(src, &sr, dst, dstrect, the_args);
        }
        dstrect->w = dstrect->h = 0;
        return 0;
}


int pygame_AlphaBlit (SDL_Surface *src, SDL_Rect *srcrect,
                   SDL_Surface *dst, SDL_Rect *dstrect)
{
    return pygame_Blit(src, srcrect, dst, dstrect, 0);
}

void pygame_stretch(SDL_Surface *src, SDL_Surface *dst) {
	int looph, loopw;

	Uint8* srcrow = (Uint8*)src->pixels;
	Uint8* dstrow = (Uint8*)dst->pixels;

	int srcpitch = src->pitch;
	int dstpitch = dst->pitch;

	int dstwidth = dst->w;
	int dstheight = dst->h;
	int dstwidth2 = dst->w << 1;
	int dstheight2 = dst->h << 1;

	int srcwidth2 = src->w << 1;
	int srcheight2 = src->h << 1;

	int w_err, h_err = srcheight2 - dstheight2;


	switch (SDL_BYTESPERPIXEL(src->format)) {
	case 1:
		for (looph = 0; looph < dstheight; ++looph) {
			Uint8 *srcpix = (Uint8*)srcrow, *dstpix = (Uint8*)dstrow;
			w_err = srcwidth2 - dstwidth2;
			for (loopw = 0; loopw < dstwidth; ++ loopw) {
				*dstpix++ = *srcpix;
				while (w_err >= 0) {++srcpix; w_err -= dstwidth2;}
				w_err += srcwidth2;
			}
			while (h_err >= 0) {srcrow += srcpitch; h_err -= dstheight2;}
			dstrow += dstpitch;
			h_err += srcheight2;
		}break;
	case 2:
		for (looph = 0; looph < dstheight; ++looph) {
			Uint16 *srcpix = (Uint16*)srcrow, *dstpix = (Uint16*)dstrow;
			w_err = srcwidth2 - dstwidth2;
			for (loopw = 0; loopw < dstwidth; ++ loopw) {
				*dstpix++ = *srcpix;
				while (w_err >= 0) {++srcpix; w_err -= dstwidth2;}
				w_err += srcwidth2;
			}
			while (h_err >= 0) {srcrow += srcpitch; h_err -= dstheight2;}
			dstrow += dstpitch;
			h_err += srcheight2;
		}break;
	case 3:
		for (looph = 0; looph < dstheight; ++looph) {
			Uint8 *srcpix = (Uint8*)srcrow, *dstpix = (Uint8*)dstrow;
			w_err = srcwidth2 - dstwidth2;
			for (loopw = 0; loopw < dstwidth; ++ loopw) {
				dstpix[0] = srcpix[0]; dstpix[1] = srcpix[1]; dstpix[2] = srcpix[2];
				dstpix += 3;
				while (w_err >= 0) {srcpix+=3; w_err -= dstwidth2;}
				w_err += srcwidth2;
			}
			while (h_err >= 0) {srcrow += srcpitch; h_err -= dstheight2;}
			dstrow += dstpitch;
			h_err += srcheight2;
		}break;
	default: /*case 4:*/
		for (looph = 0; looph < dstheight; ++looph) {
			Uint32 *srcpix = (Uint32*)srcrow, *dstpix = (Uint32*)dstrow;
			w_err = srcwidth2 - dstwidth2;
			for (loopw = 0; loopw < dstwidth; ++ loopw) {
				*dstpix++ = *srcpix;
				while (w_err >= 0) {++srcpix; w_err -= dstwidth2;}
				w_err += srcwidth2;
			}
			while (h_err >= 0) {srcrow += srcpitch; h_err -= dstheight2;}
			dstrow += dstpitch;
			h_err += srcheight2;
		}break;
	}
}
