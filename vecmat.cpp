// c47edit - Scene editor for HM C47
// Copyright (C) 2018 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#include "vecmat.h"

Matrix Matrix::getTranslationMatrix(const Vector3 & translation)
{
	Matrix m = getIdentity();
	m._41 = translation.x;
	m._42 = translation.y;
	m._43 = translation.z;
	return m;
}

Matrix Matrix::getRotationXMatrix(float radians)
{
	Matrix m = getIdentity();
	m.m[1][1] = m.m[2][2] = cos(radians);
	m.m[1][2] = sin(radians);
	m.m[2][1] = -m.m[1][2];
	return m;
}

Matrix Matrix::getRotationYMatrix(float radians)
{
	Matrix m = getIdentity();
	m.m[0][0] = m.m[2][2] = cos(radians);
	m.m[2][0] = sin(radians);
	m.m[0][2] = -m.m[2][0];
	return m;
}

Matrix Matrix::getRotationZMatrix(float radians)
{
	Matrix m = getIdentity();
	m.m[0][0] = m.m[1][1] = cos(radians);
	m.m[0][1] = sin(radians);
	m.m[1][0] = -m.m[0][1];
	return m;
}

Matrix Matrix::getScaleMatrix(const Vector3 & scale)
{
	Matrix m = getZeroMatrix();
	m._11 = scale.x;
	m._22 = scale.y;
	m._33 = scale.z;
	m._44 = 1.0f;
	return m;
}

Matrix Matrix::getLHOrthoMatrix(float w, float h, float zn, float zf)
{
	Matrix m = getZeroMatrix();
	m._11 = 2.0f / w;
	m._22 = 2.0f / h;
	m._33 = 1.0f / (zn - zf);
	m._43 = zn / (zn - zf);
	m._44 = 1.0f;
	return m;
}

Matrix Matrix::getLHPerspectiveMatrix(float fovy, float aspect, float zn, float zf)
{
	float ys = 1 / tan(fovy / 2);
	float xs = ys / aspect;
	Matrix m = getZeroMatrix();
	m.m[0][0] = xs;
	m.m[1][1] = ys;
	m.m[2][2] = zf / (zf - zn);
	m.m[2][3] = 1;
	m.m[3][2] = zn * zf / (zn - zf);
	return m;
}

Matrix Matrix::getLHLookAtViewMatrix(const Vector3 & eye, const Vector3 & at, const Vector3 & up)
{
	Vector3 ax, ay, az;
	az = (at - eye).normal();
	ax = up.cross(az).normal();
	ay = az.cross(ax);

	Matrix m = getZeroMatrix();
	m.m[0][0] = ax.x; m.m[0][1] = ay.x; m.m[0][2] = az.x;
	m.m[1][0] = ax.y; m.m[1][1] = ay.y; m.m[1][2] = az.y;
	m.m[2][0] = ax.z; m.m[2][1] = ay.z; m.m[2][2] = az.z;
	m.m[3][0] = -ax.dot(eye);
	m.m[3][1] = -ay.dot(eye);
	m.m[3][2] = -az.dot(eye);
	m.m[3][3] = 1.0f;
	return m;
}

Matrix Matrix::getRHOrthoMatrix(float w, float h, float zn, float zf)
{
	Matrix m = getZeroMatrix();
	m._11 = 2.0f / w;
	m._22 = 2.0f / h;
	m._33 = 1.0f / (zn - zf);
	m._43 = zn / (zn - zf);
	m._44 = 1.0f;
	return m;
}

Matrix Matrix::getRHPerspectiveMatrix(float fovy, float aspect, float zn, float zf)
{
	float ys = 1 / tan(fovy / 2);
	float xs = ys / aspect;
	Matrix m = getZeroMatrix();
	m.m[0][0] = xs;
	m.m[1][1] = ys;
	m.m[2][2] = zf / (zn - zf);
	m.m[2][3] = -1;
	m.m[3][2] = zn * zf / (zn - zf);
	return m;
}

Matrix Matrix::getRHLookAtViewMatrix(const Vector3 & eye, const Vector3 & at, const Vector3 & up)
{
	Vector3 ax, ay, az;
	az = (eye - at).normal();
	ax = up.cross(az).normal();
	ay = az.cross(ax);

	Matrix m = getZeroMatrix();
	m.m[0][0] = ax.x; m.m[0][1] = ay.x; m.m[0][2] = az.x;
	m.m[1][0] = ax.y; m.m[1][1] = ay.y; m.m[1][2] = az.y;
	m.m[2][0] = ax.z; m.m[2][1] = ay.z; m.m[2][2] = az.z;
	m.m[3][0] = -ax.dot(eye);
	m.m[3][1] = -ay.dot(eye);
	m.m[3][2] = -az.dot(eye);
	m.m[3][3] = 1.0f;
	return m;
}

Matrix Matrix::multiplyMatrices(const Matrix & a, const Matrix & b)
{
	Matrix m;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			m.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]
				+ a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
		}
	}
	return m;
}

Vector3 Matrix::getTranslationVector() const
{
	return Vector3(_41, _42, _43);
}

Vector3 Matrix::getScalingVector() const
{
	// NOTE: Result is positive, what about negative scaling?
	Vector3 rvec;
	for (int i = 0; i < 3; i++) {
		Vector3 axis{ m[0][i], m[1][i], m[2][i] };
		rvec.coord[i] = axis.len3();
	}
	return rvec;
}

Matrix Matrix::getInverse4x3() const
{
	Matrix inv = Matrix::getIdentity();
	for (int i = 0; i < 3; i++) {
		int i_1 = (i + 1) % 3, i_2 = (i + 2) % 3;
		for (int j = 0; j < 3; j++) {
			int j_1 = (j + 1) % 3, j_2 = (j + 2) % 3;
			inv.m[j][i] = m[i_1][j_1] * m[i_2][j_2] - m[i_1][j_2] * m[i_2][j_1];
		}
	}
	return Matrix::getTranslationMatrix(-getTranslationVector()) * inv;
}

Matrix Matrix::getTranspose() const
{
	Matrix t;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			t.m[c][r] = m[r][c];
	return t;
}

Vector3 Vector3::transform(const Matrix & m) const
{
	const Vector3 &a = *this;
	Vector3 v;
	v.x = a.x * m.m[0][0] + a.y * m.m[1][0] + a.z * m.m[2][0] + m.m[3][0];
	v.y = a.x * m.m[0][1] + a.y * m.m[1][1] + a.z * m.m[2][1] + m.m[3][1];
	v.z = a.x * m.m[0][2] + a.y * m.m[1][2] + a.z * m.m[2][2] + m.m[3][2];
	return v;
}

Vector3 Vector3::transformNormal(const Matrix& m) const
{
	const Vector3& a = *this;
	Vector3 v;
	v.x = a.x * m.m[0][0] + a.y * m.m[1][0] + a.z * m.m[2][0];
	v.y = a.x * m.m[0][1] + a.y * m.m[1][1] + a.z * m.m[2][1];
	v.z = a.x * m.m[0][2] + a.y * m.m[1][2] + a.z * m.m[2][2];
	return v;
}

Vector3 Vector3::transformScreenCoords(const Matrix &m) const
{
	const Vector3 &a = *this;
	Vector3 v;
	v.x = a.x * m.m[0][0] + a.y * m.m[1][0] + a.z * m.m[2][0] + m.m[3][0];
	v.y = a.x * m.m[0][1] + a.y * m.m[1][1] + a.z * m.m[2][1] + m.m[3][1];
	v.z = a.x * m.m[0][2] + a.y * m.m[1][2] + a.z * m.m[2][2] + m.m[3][2];
	float w = a.x * m.m[0][3] + a.y * m.m[1][3] + a.z * m.m[2][3] + m.m[3][3];
	return v / w;
}