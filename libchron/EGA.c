#include "EGA.h"
#include "libutils/BitTwiddling.h"
#include "libutils/CheckedMemory.h"
#include "Renderer.h"

#include <string.h>

static void _buildColorTable(ColorRGB *table) {
   byte i;
   //                   00 01  10   11
   byte rgbLookup[] = { 0, 85, 170, 255 };
   for (i = 0; i < EGA_COLORS; ++i) {
      byte shift = 5;

      byte r = getBit(i, shift--);
      byte g = getBit(i, shift--);
      byte b = getBit(i, shift--);
      byte R = getBit(i, shift--);
      byte G = getBit(i, shift--);
      byte B = getBit(i, shift);

      byte rgb_r = rgbLookup[(R << 1) + r];
      byte rgb_g = rgbLookup[(G << 1) + g];
      byte rgb_b = rgbLookup[(B << 1) + b];

      table[i] = (ColorRGB) { rgb_r, rgb_g, rgb_b };
   }
}

ColorRGB egaGetColor(EGAColor c) {
   static ColorRGB lookup[EGA_COLORS] = { 0 };
   static bool loaded = 0;

   if (!loaded) {
      _buildColorTable(lookup);
      loaded = 1;
   }

   return lookup[c];
}

/*
pixel data organization

alpha is stored as 1 bit per pixel in scanlines
pixel data is stored plane-interleaved with 4 bits per pixel
the lower 4 bits are leftmost in the image and higher 4 bits are rightmost

[byte 0  ] [byte 1]
[MSB][LSB] [MSB][LSB] 
[x:1][x:0] [x:3][x:2]

alpha scanlines are bytealigned so use alphaSLwidth for traversing

pixel scanlines need enough room for a 4bit right-shift and 
so may have an extra byte at the end, use pixelSLWidth for traversiong

pixel data is also stored in an offset form where ewach scanline is left-shifted by 4
so that the scanline can be memcpy'd directly onto the target if its not aligned
this buffer is generated on-demand with offsetDirty
*/

struct EGATexture {
   u32 w, h;
   EGARegion fullRegion;

   u32 alphaSLWidth; //size in bytes of the alpha channel scanlines
   u32 pixelSLWidth; //size in bytes of the pixel datga scanlines
   u32 pixelCount;

   // 1 bit per pixel, 0 for transparent
   byte *alphaChannel;

   byte *pixelData;
   byte *pixelDataOffset;

   Texture *decoded;
   ColorRGBA *decodePixels;

   bool offsetDirty, decodeDirty;
};

static void _freeTextureBuffers(EGATexture *self) {
   if (self->alphaChannel) {
      checkedFree(self->alphaChannel);
      self->alphaChannel = NULL;
   }

   if (self->decoded) {
      textureDestroy(self->decoded);
      self->decoded = NULL;
   }

   if (self->decodePixels) {
      checkedFree(self->decodePixels);
      self->decodePixels = NULL;
   }

   if (self->pixelData) {
      checkedFree(self->pixelData);
      self->pixelData = NULL;
   }

   if (self->pixelDataOffset) {
      checkedFree(self->pixelDataOffset);
      self->pixelDataOffset = NULL;
   }
}


EGATexture *egaTextureCreate(u32 width, u32 height) {
   EGATexture *self = checkedCalloc(1, sizeof(EGATexture));
   
   egaTextureResize(self, width, height);

   return self;
}
void egaTextureDestroy(EGATexture *self) {
   _freeTextureBuffers(self);
   checkedFree(self);
}

EGATexture *egaTextureEncode(Texture *source, EGAPalette *targetPalette, EGAPalette *resultPalette) {

}
Texture *egaTextureDecode(EGATexture *self, EGAPalette *palette) {
   if (!self->decoded) {
      self->decoded = textureCreateCustom(self->w, self->h, RepeatType_Clamp, FilterType_Linear);
   }

   if (!self->decodePixels) {
      self->decodePixels = (ColorRGBA*)checkedCalloc(self->pixelCount, sizeof(ColorRGBA));
   }

   if (self->decodeDirty) {      
      memset(self->decodePixels, 0, self->pixelCount);
      u32 x, y;
      u32 asl = 0, psl = 0, dsl = 0;
      for (y = 0; y < self->h; ++y) {        

         for (x = 0; x < self->w; ++x) {
            if (self->alphaChannel[asl + (x >> 3)] & (1 << (x & 7))) {
               byte twoPix = self->pixelData[psl + (x >> 1)];
               EGAPColor pIdx = x & 1 ? twoPix >> 4 : twoPix & 15;
               ColorRGB rgb = egaGetColor(palette->colors[pIdx]);

               self->decodePixels[dsl + x] = (ColorRGBA) { rgb.r, rgb.g, rgb.b, 255 };
            }
         }

         asl += self->alphaSLWidth; //alpha byte position
         psl += self->pixelSLWidth; //pixel byte position
         dsl += self->w; //decode pixel position
      }

      textureSetPixels(self->decoded, (byte*)self->decodePixels);
      self->decodeDirty = false;
   }

  return self->decoded;
}

int egaTextureSerialize(EGATexture *self, byte **outBuff, u64 *size) {

}
EGATexture *egaTextureDeserialize(byte *buff, u64 size) {

}

void egaTextureResize(EGATexture *self, u32 width, u32 height) {
   if (width == self->w && height == self->h) {
      return;
   }

   _freeTextureBuffers(self);

   self->w = width;
   self->h = height;
   self->pixelCount = self->w * self->h;
   self->fullRegion = (EGARegion) { 0, 0, self->w, self->h };

   // w/8 + w%8 ? 1 : 0
   self->alphaSLWidth = (self->w >> 3) + ((self->w & 7) ? 1 : 0);

   //add an extra byte if width is even (odd has extra half byte)
   self->pixelSLWidth = (self->w >> 1) + ((self->w & 1) ? 0 : 1);

   self->alphaChannel = (byte*)checkedCalloc(self->h, self->alphaSLWidth);
   self->pixelData = (byte*)checkedCalloc(self->h, self->pixelSLWidth);
}

u32 egaTextureGetWidth(EGATexture *self) { return self->w; }
u32 egaTextureGetHeight(EGATexture *self) {return self->h; }
EGARegion *egaTextureGetFullRegion(EGATexture *self) { return &self->fullRegion; }

EGAPColor egaTextureGetColorAt(EGATexture *self, EGARegion *vp, u32 x, u32 y) {

}

struct EGAFontFactory {
   EMPTY_STRUCT;
};

struct EGAFont {
   EMPTY_STRUCT;
};

EGAFontFactory *egaFontFactoryCreate(EGATexture *font) {

}
void egaFontFactoryDestroy(EGAFontFactory *self) {

}
EGAFont *egaFontFactoryGetFont(EGAFontFactory *self, EGAColor bgColor, EGAColor fgColor) {

}

void egaClear(EGATexture *target, EGARegion *vp, EGAPColor color) { }
void egaRenderTexture(EGATexture *target, EGARegion *vp, int x, int y, EGATexture *tex) { }
void egaRenderTexturePartial(EGATexture *target, EGARegion *vp, int x, int y, EGATexture *tex, int texX, int texY, int texWidth, int texHeight) { }
void egaRenderPoint(EGATexture *target, EGARegion *vp, int x, int y, EGAPColor color) { }
void egaRenderLine(EGATexture *target, EGARegion *vp, int x1, int y1, int x2, int y2, EGAPColor color) { }
void egaRenderLineRect(EGATexture *target, EGARegion *vp, int left, int top, int right, int bottom, EGAPColor color) { }
void egaRenderRect(EGATexture *target, EGARegion *vp, int left, int top, int right, int bottom, EGAPColor color) { }

void egaRenderCircle(EGATexture *target, EGARegion *vp, int x, int y, int radius, EGAPColor color) { }
void egaRenderEllipse(EGATexture *target, EGARegion *vp, int xc, int yc, int width, int height, EGAPColor color) { }
void egaRenderEllipseQB(EGATexture *target, EGARegion *vp, int xc, int yc, int radius, EGAPColor color, double aspect) { }

void egaRenderTextSingleChar(EGATexture *target, const char c, int x, int y, EGAFont *font, int spaces) { }
void egaRenderText(EGATexture *target, const char *text, int x, int y, EGAFont *font) { }
void egaRenderTextWithoutSpaces(EGATexture *target, const char *text, int x, int y, EGAFont *font) { }