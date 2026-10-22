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

// D3DX8 math functions used by the game, for builds without d3dx8.lib (x64).
// Ported from GeneralsX (fbraz3/GeneralsX, CompatLib/Source/d3dx8math.cpp); the GLM based inverse
// is replaced by a plain cofactor inverse so that no extra library is needed.
// D3DX matrices are row major with row vectors: v' = v * M.

#include <windows.h>
#include <d3dx8.h>
#include <math.h>

D3DXMATRIX* WINAPI D3DXMatrixInverse(D3DXMATRIX *pOut, FLOAT *pDeterminant, CONST D3DXMATRIX *pM)
{
	if (!pOut || !pM)
		return nullptr;

	const FLOAT *m = &pM->m[0][0];
	FLOAT inv[16];

	inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8]  =  m[4]*m[9] *m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9] *m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
	inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9]  = -m[0]*m[9] *m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] =  m[0]*m[9] *m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
	inv[2]  =  m[1]*m[6] *m[15] - m[1]*m[7] *m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
	inv[6]  = -m[0]*m[6] *m[15] + m[0]*m[7] *m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
	inv[10] =  m[0]*m[5] *m[15] - m[0]*m[7] *m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5] *m[14] + m[0]*m[6] *m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
	inv[3]  = -m[1]*m[6] *m[11] + m[1]*m[7] *m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9] *m[2]*m[7]  + m[9] *m[3]*m[6];
	inv[7]  =  m[0]*m[6] *m[11] - m[0]*m[7] *m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8] *m[2]*m[7]  - m[8] *m[3]*m[6];
	inv[11] = -m[0]*m[5] *m[11] + m[0]*m[7] *m[9]  + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8] *m[1]*m[7]  + m[8] *m[3]*m[5];
	inv[15] =  m[0]*m[5] *m[10] - m[0]*m[6] *m[9]  - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8] *m[1]*m[6]  - m[8] *m[2]*m[5];

	const FLOAT det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];

	if (pDeterminant)
		*pDeterminant = det;

	// Like D3DX: a singular matrix has no inverse
	if (det == 0.0f)
		return nullptr;

	const FLOAT invDet = 1.0f / det;
	FLOAT *out = &pOut->m[0][0];
	for (int i = 0; i < 16; ++i)
		out[i] = inv[i] * invDet;

	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixScaling(D3DXMATRIX *pOut, FLOAT sx, FLOAT sy, FLOAT sz)
{
	if (!pOut) return nullptr;
	pOut->m[0][0] = sx;   pOut->m[0][1] = 0.0f; pOut->m[0][2] = 0.0f; pOut->m[0][3] = 0.0f;
	pOut->m[1][0] = 0.0f; pOut->m[1][1] = sy;   pOut->m[1][2] = 0.0f; pOut->m[1][3] = 0.0f;
	pOut->m[2][0] = 0.0f; pOut->m[2][1] = 0.0f; pOut->m[2][2] = sz;   pOut->m[2][3] = 0.0f;
	pOut->m[3][0] = 0.0f; pOut->m[3][1] = 0.0f; pOut->m[3][2] = 0.0f; pOut->m[3][3] = 1.0f;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixTranslation(D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z)
{
	if (!pOut) return nullptr;
	pOut->m[0][0] = 1.0f; pOut->m[0][1] = 0.0f; pOut->m[0][2] = 0.0f; pOut->m[0][3] = 0.0f;
	pOut->m[1][0] = 0.0f; pOut->m[1][1] = 1.0f; pOut->m[1][2] = 0.0f; pOut->m[1][3] = 0.0f;
	pOut->m[2][0] = 0.0f; pOut->m[2][1] = 0.0f; pOut->m[2][2] = 1.0f; pOut->m[2][3] = 0.0f;
	pOut->m[3][0] = x;    pOut->m[3][1] = y;    pOut->m[3][2] = z;    pOut->m[3][3] = 1.0f;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM1, CONST D3DXMATRIX *pM2)
{
	if (!pOut || !pM1 || !pM2) return nullptr;
	D3DXMATRIX temp;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			temp.m[i][j] = pM1->m[i][0] * pM2->m[0][j] +
			               pM1->m[i][1] * pM2->m[1][j] +
			               pM1->m[i][2] * pM2->m[2][j] +
			               pM1->m[i][3] * pM2->m[3][j];
		}
	}
	*pOut = temp;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixTranspose(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM)
{
	if (!pOut || !pM) return nullptr;
	D3DXMATRIX temp;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			temp.m[i][j] = pM->m[j][i];
		}
	}
	*pOut = temp;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationZ(D3DXMATRIX *pOut, FLOAT Angle)
{
	if (!pOut) return nullptr;
	const FLOAT fSin = sinf(Angle);
	const FLOAT fCos = cosf(Angle);

	pOut->m[0][0] = fCos;  pOut->m[0][1] = fSin; pOut->m[0][2] = 0.0f; pOut->m[0][3] = 0.0f;
	pOut->m[1][0] = -fSin; pOut->m[1][1] = fCos; pOut->m[1][2] = 0.0f; pOut->m[1][3] = 0.0f;
	pOut->m[2][0] = 0.0f;  pOut->m[2][1] = 0.0f; pOut->m[2][2] = 1.0f; pOut->m[2][3] = 0.0f;
	pOut->m[3][0] = 0.0f;  pOut->m[3][1] = 0.0f; pOut->m[3][2] = 0.0f; pOut->m[3][3] = 1.0f;
	return pOut;
}

D3DXVECTOR4* WINAPI D3DXVec3Transform(D3DXVECTOR4 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DXMATRIX *pM)
{
	if (!pOut || !pV || !pM) return nullptr;
	D3DXVECTOR4 temp;
	temp.x = pV->x * pM->m[0][0] + pV->y * pM->m[1][0] + pV->z * pM->m[2][0] + pM->m[3][0];
	temp.y = pV->x * pM->m[0][1] + pV->y * pM->m[1][1] + pV->z * pM->m[2][1] + pM->m[3][1];
	temp.z = pV->x * pM->m[0][2] + pV->y * pM->m[1][2] + pV->z * pM->m[2][2] + pM->m[3][2];
	temp.w = pV->x * pM->m[0][3] + pV->y * pM->m[1][3] + pV->z * pM->m[2][3] + pM->m[3][3];
	*pOut = temp;
	return pOut;
}

D3DXVECTOR4* WINAPI D3DXVec4Transform(D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV, CONST D3DXMATRIX *pM)
{
	if (!pOut || !pV || !pM) return nullptr;
	D3DXVECTOR4 temp;
	temp.x = pV->x * pM->m[0][0] + pV->y * pM->m[1][0] + pV->z * pM->m[2][0] + pV->w * pM->m[3][0];
	temp.y = pV->x * pM->m[0][1] + pV->y * pM->m[1][1] + pV->z * pM->m[2][1] + pV->w * pM->m[3][1];
	temp.z = pV->x * pM->m[0][2] + pV->y * pM->m[1][2] + pV->z * pM->m[2][2] + pV->w * pM->m[3][2];
	temp.w = pV->x * pM->m[0][3] + pV->y * pM->m[1][3] + pV->z * pM->m[2][3] + pV->w * pM->m[3][3];
	*pOut = temp;
	return pOut;
}
