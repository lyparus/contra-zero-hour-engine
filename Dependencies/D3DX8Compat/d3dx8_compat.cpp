/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// D3DX8 texture and helper functions used by the game, for builds without d3dx8.lib (x64).
// Based on GeneralsX (fbraz3/GeneralsX, CompatLib/Source/d3dx8_compat.cpp), which in parts follows Wine.
// Differences: no GLI dependency, D3DXLoadSurfaceFromSurface honours source/destination rectangles and
// converts between the uncompressed formats the game uses, cube/volume textures are created on the device.
// Not implemented (the game copes with failure): D3DXCreateTextureFromFileExA (unused) and
// D3DXAssembleShader (water pixel shaders fall back to the fixed function path).

#include <windows.h>
#include <d3dx8.h>
#include <stdio.h>
#include <string.h>

namespace
{

typedef unsigned int  uint32;
typedef unsigned char uint8;

UINT Default_To(UINT value, UINT fallback)
{
	return (value == D3DX_DEFAULT || value == 0) ? fallback : value;
}

// Bytes per pixel of the uncompressed formats we can convert, 0 for everything else
unsigned Bytes_Per_Pixel(D3DFORMAT format)
{
	switch (format)
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
		return 4;
	case D3DFMT_R8G8B8:
		return 3;
	case D3DFMT_R5G6B5:
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_A4R4G4B4:
	case D3DFMT_X4R4G4B4:
	case D3DFMT_A8L8:
		return 2;
	case D3DFMT_A8:
	case D3DFMT_L8:
		return 1;
	default:
		return 0;
	}
}

bool Is_Compressed(D3DFORMAT format)
{
	return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3
		|| format == D3DFMT_DXT4 || format == D3DFMT_DXT5;
}

inline uint32 Expand4(uint32 v) { return v * 17; }
inline uint32 Expand5(uint32 v) { return (v << 3) | (v >> 2); }
inline uint32 Expand6(uint32 v) { return (v << 2) | (v >> 4); }

uint32 Read_ARGB(const uint8 *p, D3DFORMAT format)
{
	switch (format)
	{
	case D3DFMT_A8R8G8B8:
		return *(const uint32 *)p;
	case D3DFMT_X8R8G8B8:
		return *(const uint32 *)p | 0xFF000000;
	case D3DFMT_R8G8B8:
		return 0xFF000000 | (p[2] << 16) | (p[1] << 8) | p[0];
	case D3DFMT_R5G6B5:
	{
		const uint32 v = *(const unsigned short *)p;
		return 0xFF000000 | (Expand5((v >> 11) & 0x1F) << 16) | (Expand6((v >> 5) & 0x3F) << 8) | Expand5(v & 0x1F);
	}
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
	{
		const uint32 v = *(const unsigned short *)p;
		const uint32 a = (format == D3DFMT_X1R5G5B5 || (v & 0x8000)) ? 0xFF : 0;
		return (a << 24) | (Expand5((v >> 10) & 0x1F) << 16) | (Expand5((v >> 5) & 0x1F) << 8) | Expand5(v & 0x1F);
	}
	case D3DFMT_A4R4G4B4:
	case D3DFMT_X4R4G4B4:
	{
		const uint32 v = *(const unsigned short *)p;
		const uint32 a = (format == D3DFMT_X4R4G4B4) ? 0xFF : Expand4((v >> 12) & 0xF);
		return (a << 24) | (Expand4((v >> 8) & 0xF) << 16) | (Expand4((v >> 4) & 0xF) << 8) | Expand4(v & 0xF);
	}
	case D3DFMT_A8L8:
	{
		const uint32 l = p[0];
		return (p[1] << 24) | (l << 16) | (l << 8) | l;
	}
	case D3DFMT_A8:
		return p[0] << 24;
	case D3DFMT_L8:
	{
		const uint32 l = p[0];
		return 0xFF000000 | (l << 16) | (l << 8) | l;
	}
	default:
		return 0;
	}
}

void Write_ARGB(uint8 *p, D3DFORMAT format, uint32 c)
{
	const uint32 a = (c >> 24) & 0xFF, r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
	switch (format)
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
		*(uint32 *)p = c;
		break;
	case D3DFMT_R8G8B8:
		p[0] = (uint8)b; p[1] = (uint8)g; p[2] = (uint8)r;
		break;
	case D3DFMT_R5G6B5:
		*(unsigned short *)p = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
		break;
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
		*(unsigned short *)p = (unsigned short)(((a >= 0x80) ? 0x8000 : 0) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
		break;
	case D3DFMT_A4R4G4B4:
	case D3DFMT_X4R4G4B4:
		*(unsigned short *)p = (unsigned short)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		break;
	case D3DFMT_A8L8:
		p[0] = (uint8)((r * 77 + g * 150 + b * 29) >> 8);
		p[1] = (uint8)a;
		break;
	case D3DFMT_A8:
		p[0] = (uint8)a;
		break;
	case D3DFMT_L8:
		p[0] = (uint8)((r * 77 + g * 150 + b * 29) >> 8);
		break;
	default:
		break;
	}
}

inline const uint8 *Pixel(const D3DLOCKED_RECT &lock, unsigned bpp, unsigned x, unsigned y)
{
	return (const uint8 *)lock.pBits + y * lock.Pitch + x * bpp;
}

uint32 Lerp_ARGB(uint32 c0, uint32 c1, float t)
{
	uint32 result = 0;
	for (int shift = 0; shift < 32; shift += 8)
	{
		const float v0 = (float)((c0 >> shift) & 0xFF);
		const float v1 = (float)((c1 >> shift) & 0xFF);
		const uint32 v = (uint32)(v0 + (v1 - v0) * t + 0.5f);
		result |= (v > 0xFF ? 0xFF : v) << shift;
	}
	return result;
}

RECT Whole_Rect(const D3DSURFACE_DESC &desc)
{
	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = (LONG)desc.Width;
	rect.bottom = (LONG)desc.Height;
	return rect;
}

} // namespace

HRESULT WINAPI D3DXGetErrorStringA(HRESULT hr, LPSTR pBuffer, UINT BufferLen)
{
	if (pBuffer == nullptr || BufferLen == 0)
		return D3DERR_INVALIDCALL;

	snprintf(pBuffer, BufferLen, "HRESULT 0x%08X", (unsigned)hr);
	return D3D_OK;
}

HRESULT WINAPI D3DXCreateTexture(
	LPDIRECT3DDEVICE8 pDevice,
	UINT Width,
	UINT Height,
	UINT MipLevels,
	DWORD Usage,
	D3DFORMAT Format,
	D3DPOOL Pool,
	LPDIRECT3DTEXTURE8 *ppTexture)
{
	if (ppTexture == nullptr)
		return D3DERR_INVALIDCALL;
	*ppTexture = nullptr;
	if (pDevice == nullptr)
		return D3DERR_INVALIDCALL;

	// D3DX semantics: 0 or D3DX_DEFAULT size means 1, mip count 0 or D3DX_DEFAULT means a full chain
	Width = Default_To(Width, 1);
	Height = Default_To(Height, 1);
	if (MipLevels == D3DX_DEFAULT)
		MipLevels = 0;
	if (Format == D3DFMT_UNKNOWN)
		Format = D3DFMT_A8R8G8B8;

	return pDevice->CreateTexture(Width, Height, MipLevels, Usage, Format, Pool, ppTexture);
}

HRESULT WINAPI D3DXCreateCubeTexture(
	LPDIRECT3DDEVICE8 pDevice,
	UINT Size,
	UINT MipLevels,
	DWORD Usage,
	D3DFORMAT Format,
	D3DPOOL Pool,
	LPDIRECT3DCUBETEXTURE8 *ppCubeTexture)
{
	if (ppCubeTexture == nullptr)
		return D3DERR_INVALIDCALL;
	*ppCubeTexture = nullptr;
	if (pDevice == nullptr)
		return D3DERR_INVALIDCALL;

	Size = Default_To(Size, 1);
	if (MipLevels == D3DX_DEFAULT)
		MipLevels = 0;
	if (Format == D3DFMT_UNKNOWN)
		Format = D3DFMT_A8R8G8B8;

	return pDevice->CreateCubeTexture(Size, MipLevels, Usage, Format, Pool, ppCubeTexture);
}

HRESULT WINAPI D3DXCreateVolumeTexture(
	LPDIRECT3DDEVICE8 pDevice,
	UINT Width,
	UINT Height,
	UINT Depth,
	UINT MipLevels,
	DWORD Usage,
	D3DFORMAT Format,
	D3DPOOL Pool,
	LPDIRECT3DVOLUMETEXTURE8 *ppVolumeTexture)
{
	if (ppVolumeTexture == nullptr)
		return D3DERR_INVALIDCALL;
	*ppVolumeTexture = nullptr;
	if (pDevice == nullptr)
		return D3DERR_INVALIDCALL;

	Width = Default_To(Width, 1);
	Height = Default_To(Height, 1);
	Depth = Default_To(Depth, 1);
	if (MipLevels == D3DX_DEFAULT)
		MipLevels = 0;
	if (Format == D3DFMT_UNKNOWN)
		Format = D3DFMT_A8R8G8B8;

	return pDevice->CreateVolumeTexture(Width, Height, Depth, MipLevels, Usage, Format, Pool, ppVolumeTexture);
}

HRESULT WINAPI D3DXCreateTextureFromFileExA(
	LPDIRECT3DDEVICE8 pDevice,
	LPCSTR pSrcFile,
	UINT Width,
	UINT Height,
	UINT MipLevels,
	DWORD Usage,
	D3DFORMAT Format,
	D3DPOOL Pool,
	DWORD Filter,
	DWORD MipFilter,
	D3DCOLOR ColorKey,
	D3DXIMAGE_INFO *pSrcInfo,
	PALETTEENTRY *pPalette,
	LPDIRECT3DTEXTURE8 *ppTexture)
{
	// Not used by the game (textures are loaded by its own TGA/DDS code)
	if (ppTexture != nullptr)
		*ppTexture = nullptr;
	return D3DERR_NOTAVAILABLE;
}

HRESULT WINAPI D3DXLoadSurfaceFromSurface(
	LPDIRECT3DSURFACE8 pDestSurface,
	CONST PALETTEENTRY *pDestPalette,
	CONST RECT *pDestRect,
	LPDIRECT3DSURFACE8 pSrcSurface,
	CONST PALETTEENTRY *pSrcPalette,
	CONST RECT *pSrcRect,
	DWORD Filter,
	D3DCOLOR ColorKey)
{
	if (pDestSurface == nullptr || pSrcSurface == nullptr)
		return D3DERR_INVALIDCALL;

	D3DSURFACE_DESC srcDesc, dstDesc;
	if (FAILED(pSrcSurface->GetDesc(&srcDesc)) || FAILED(pDestSurface->GetDesc(&dstDesc)))
		return D3DERR_INVALIDCALL;

	const RECT srcRect = pSrcRect ? *pSrcRect : Whole_Rect(srcDesc);
	const RECT dstRect = pDestRect ? *pDestRect : Whole_Rect(dstDesc);
	const int srcW = srcRect.right - srcRect.left, srcH = srcRect.bottom - srcRect.top;
	const int dstW = dstRect.right - dstRect.left, dstH = dstRect.bottom - dstRect.top;
	if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
		return D3DERR_INVALIDCALL;

	// Compressed surfaces can only be copied 1:1 in the same format
	if (Is_Compressed(srcDesc.Format) || Is_Compressed(dstDesc.Format))
	{
		if (srcDesc.Format != dstDesc.Format || srcW != dstW || srcH != dstH || pSrcRect || pDestRect)
			return D3DERR_INVALIDCALL;

		D3DLOCKED_RECT srcLock, dstLock;
		if (FAILED(pSrcSurface->LockRect(&srcLock, nullptr, D3DLOCK_READONLY)))
			return D3DERR_INVALIDCALL;
		if (FAILED(pDestSurface->LockRect(&dstLock, nullptr, 0)))
		{
			pSrcSurface->UnlockRect();
			return D3DERR_INVALIDCALL;
		}
		const unsigned blockBytes = (srcDesc.Format == D3DFMT_DXT1) ? 8 : 16;
		const unsigned rowBytes = ((srcDesc.Width + 3) / 4) * blockBytes;
		const unsigned rows = (srcDesc.Height + 3) / 4;
		for (unsigned y = 0; y < rows; ++y)
			memcpy((uint8 *)dstLock.pBits + y * dstLock.Pitch, (const uint8 *)srcLock.pBits + y * srcLock.Pitch, rowBytes);
		pDestSurface->UnlockRect();
		pSrcSurface->UnlockRect();
		return D3D_OK;
	}

	const unsigned srcBpp = Bytes_Per_Pixel(srcDesc.Format);
	const unsigned dstBpp = Bytes_Per_Pixel(dstDesc.Format);
	if (srcBpp == 0 || dstBpp == 0)
		return D3DERR_INVALIDCALL;

	D3DLOCKED_RECT srcLock, dstLock;
	if (FAILED(pSrcSurface->LockRect(&srcLock, &srcRect, D3DLOCK_READONLY)))
		return D3DERR_INVALIDCALL;
	if (FAILED(pDestSurface->LockRect(&dstLock, &dstRect, 0)))
	{
		pSrcSurface->UnlockRect();
		return D3DERR_INVALIDCALL;
	}

	const DWORD filterType = (Filter == D3DX_DEFAULT) ? D3DX_FILTER_BOX : (Filter & 0xFF);
	const bool point = (filterType == D3DX_FILTER_NONE || filterType == D3DX_FILTER_POINT);
	const bool sameSize = (srcW == dstW && srcH == dstH);
	const bool downscale = (dstW <= srcW && dstH <= srcH);

	for (int y = 0; y < dstH; ++y)
	{
		uint8 *dstRow = (uint8 *)dstLock.pBits + y * dstLock.Pitch;

		if (sameSize && srcDesc.Format == dstDesc.Format && ColorKey == 0)
		{
			memcpy(dstRow, Pixel(srcLock, srcBpp, 0, y), dstW * dstBpp);
			continue;
		}

		for (int x = 0; x < dstW; ++x)
		{
			uint32 color;

			if (sameSize || point)
			{
				const unsigned sx = (unsigned)(x * srcW / dstW);
				const unsigned sy = (unsigned)(y * srcH / dstH);
				color = Read_ARGB(Pixel(srcLock, srcBpp, sx, sy), srcDesc.Format);
			}
			else if (downscale)
			{
				// Box filter: average the source pixels covered by this destination pixel
				const int x0 = x * srcW / dstW, y0 = y * srcH / dstH;
				int x1 = (x + 1) * srcW / dstW, y1 = (y + 1) * srcH / dstH;
				if (x1 <= x0) x1 = x0 + 1;
				if (y1 <= y0) y1 = y0 + 1;

				uint32 sum[4] = { 0, 0, 0, 0 };
				uint32 count = 0;
				for (int sy = y0; sy < y1; ++sy)
				{
					for (int sx = x0; sx < x1; ++sx)
					{
						const uint32 c = Read_ARGB(Pixel(srcLock, srcBpp, sx, sy), srcDesc.Format);
						sum[0] += c & 0xFF;
						sum[1] += (c >> 8) & 0xFF;
						sum[2] += (c >> 16) & 0xFF;
						sum[3] += (c >> 24) & 0xFF;
						++count;
					}
				}
				const uint32 half = count / 2;
				color = ((sum[0] + half) / count) | (((sum[1] + half) / count) << 8)
					| (((sum[2] + half) / count) << 16) | (((sum[3] + half) / count) << 24);
			}
			else
			{
				// Bilinear for magnification
				float fx = (x + 0.5f) * srcW / dstW - 0.5f;
				float fy = (y + 0.5f) * srcH / dstH - 0.5f;
				if (fx < 0.0f) fx = 0.0f;
				if (fy < 0.0f) fy = 0.0f;
				int ix = (int)fx, iy = (int)fy;
				if (ix > srcW - 1) ix = srcW - 1;
				if (iy > srcH - 1) iy = srcH - 1;
				const int ix1 = (ix + 1 < srcW) ? ix + 1 : ix;
				const int iy1 = (iy + 1 < srcH) ? iy + 1 : iy;
				const float tx = fx - ix, ty = fy - iy;

				const uint32 c00 = Read_ARGB(Pixel(srcLock, srcBpp, ix,  iy),  srcDesc.Format);
				const uint32 c10 = Read_ARGB(Pixel(srcLock, srcBpp, ix1, iy),  srcDesc.Format);
				const uint32 c01 = Read_ARGB(Pixel(srcLock, srcBpp, ix,  iy1), srcDesc.Format);
				const uint32 c11 = Read_ARGB(Pixel(srcLock, srcBpp, ix1, iy1), srcDesc.Format);
				color = Lerp_ARGB(Lerp_ARGB(c00, c10, tx), Lerp_ARGB(c01, c11, tx), ty);
			}

			// Color key: matching pixels become transparent black
			if (ColorKey != 0 && (color | 0xFF000000) == (ColorKey | 0xFF000000))
				color = 0;

			Write_ARGB(dstRow + x * dstBpp, dstDesc.Format, color);
		}
	}

	pDestSurface->UnlockRect();
	pSrcSurface->UnlockRect();
	return D3D_OK;
}

// Generates the mip levels below SrcLevel from the level above them (follows Wine)
HRESULT WINAPI D3DXFilterTexture(
	LPDIRECT3DBASETEXTURE8 pBaseTexture,
	CONST PALETTEENTRY *pPalette,
	UINT SrcLevel,
	DWORD Filter)
{
	if (pBaseTexture == nullptr)
		return D3DERR_INVALIDCALL;

	if (SrcLevel == D3DX_DEFAULT)
		SrcLevel = 0;
	else if (SrcLevel >= pBaseTexture->GetLevelCount())
		return D3DERR_INVALIDCALL;

	if (Filter == D3DX_DEFAULT)
		Filter = D3DX_FILTER_BOX;

	const UINT levels = pBaseTexture->GetLevelCount();

	switch (pBaseTexture->GetType())
	{
	case D3DRTYPE_TEXTURE:
	{
		IDirect3DTexture8 *tex = (IDirect3DTexture8 *)pBaseTexture;
		for (UINT level = SrcLevel + 1; level < levels; ++level)
		{
			IDirect3DSurface8 *src = nullptr, *dst = nullptr;
			if (FAILED(tex->GetSurfaceLevel(level - 1, &src)))
				return D3DERR_INVALIDCALL;
			if (FAILED(tex->GetSurfaceLevel(level, &dst)))
			{
				src->Release();
				return D3DERR_INVALIDCALL;
			}
			const HRESULT hr = D3DXLoadSurfaceFromSurface(dst, nullptr, nullptr, src, nullptr, nullptr, Filter, 0);
			dst->Release();
			src->Release();
			if (FAILED(hr))
				return hr;
		}
		return D3D_OK;
	}
	case D3DRTYPE_CUBETEXTURE:
	{
		IDirect3DCubeTexture8 *tex = (IDirect3DCubeTexture8 *)pBaseTexture;
		for (int face = D3DCUBEMAP_FACE_POSITIVE_X; face <= D3DCUBEMAP_FACE_NEGATIVE_Z; ++face)
		{
			for (UINT level = SrcLevel + 1; level < levels; ++level)
			{
				IDirect3DSurface8 *src = nullptr, *dst = nullptr;
				if (FAILED(tex->GetCubeMapSurface((D3DCUBEMAP_FACES)face, level - 1, &src)))
					return D3DERR_INVALIDCALL;
				if (FAILED(tex->GetCubeMapSurface((D3DCUBEMAP_FACES)face, level, &dst)))
				{
					src->Release();
					return D3DERR_INVALIDCALL;
				}
				const HRESULT hr = D3DXLoadSurfaceFromSurface(dst, nullptr, nullptr, src, nullptr, nullptr, Filter, 0);
				dst->Release();
				src->Release();
				if (FAILED(hr))
					return hr;
			}
		}
		return D3D_OK;
	}
	default:
		// Volume textures are not used by the game
		return D3DERR_NOTAVAILABLE;
	}
}

HRESULT WINAPI D3DXAssembleShader(
	LPCVOID pSrcData,
	UINT SrcDataLen,
	DWORD Flags,
	LPD3DXBUFFER *ppConstants,
	LPD3DXBUFFER *ppCompiledShader,
	LPD3DXBUFFER *ppCompilationErrors)
{
	// No shader assembler on x64. Callers (water, profiler) check the result and do without the shader.
	if (ppConstants) *ppConstants = nullptr;
	if (ppCompiledShader) *ppCompiledShader = nullptr;
	if (ppCompilationErrors) *ppCompilationErrors = nullptr;
	return D3DERR_NOTAVAILABLE;
}

// Taken from Wine at ca46949 (via GeneralsX)
static UINT Get_TexCoord_Size_From_FVF(DWORD FVF, int tex_num)
{
	return (((((FVF) >> (16 + (2 * (tex_num)))) + 1) & 0x03) + 1);
}

UINT WINAPI D3DXGetFVFVertexSize(DWORD FVF)
{
	DWORD size = 0;
	const UINT numTextures = (FVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;

	if (FVF & D3DFVF_NORMAL) size += sizeof(D3DVECTOR);
	if (FVF & D3DFVF_DIFFUSE) size += sizeof(DWORD);
	if (FVF & D3DFVF_SPECULAR) size += sizeof(DWORD);
	if (FVF & D3DFVF_PSIZE) size += sizeof(DWORD);

	switch (FVF & D3DFVF_POSITION_MASK)
	{
	case D3DFVF_XYZ:    size += sizeof(D3DVECTOR); break;
	case D3DFVF_XYZRHW: size += 4 * sizeof(FLOAT); break;
	case D3DFVF_XYZB1:  size += 4 * sizeof(FLOAT); break;
	case D3DFVF_XYZB2:  size += 5 * sizeof(FLOAT); break;
	case D3DFVF_XYZB3:  size += 6 * sizeof(FLOAT); break;
	case D3DFVF_XYZB4:  size += 7 * sizeof(FLOAT); break;
	case D3DFVF_XYZB5:  size += 8 * sizeof(FLOAT); break;
	}

	for (UINT i = 0; i < numTextures; i++)
		size += Get_TexCoord_Size_From_FVF(FVF, i) * sizeof(FLOAT);

	return size;
}
