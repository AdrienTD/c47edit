// c47edit - Scene editor for HM C47
// Copyright (C) 2018-2022 AdrienTD
// Licensed under the GPL3+.
// See LICENSE file for more details.

#pragma once

#include <cmath>

struct Matrix;
struct Vector3;

// 4x4 matrix structure
struct /*alignas(16)*/ Matrix
{
	union {
		float v[16];
		float m[4][4];
		struct {
			float _11, _12, _13, _14, _21, _22, _23, _24,
				_31, _32, _33, _34, _41, _42, _43, _44;
		};
	};
	Matrix operator*(const Matrix &a) const
	{
		return multiplyMatrices(*this, a);
	}
	Matrix &operator*=(const Matrix &a)
	{
		*this = multiplyMatrices(*this, a);
		return *this;
	}
	bool operator==(const Matrix &a) const {
		for (int i = 0; i < 16; i++)
			if (v[i] != a.v[i])
				return false;
		return true;
	}
	bool operator!=(const Matrix &a) const { return !(*this == a); }

	Vector3 getTranslationVector() const;
	Vector3 getScalingVector() const;
	Matrix getInverse4x3() const;
	Matrix getTranspose() const;

	static Matrix getIdentity()
	{
		Matrix mat;
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
				mat.m[i][j] = (i == j) ? 1.0f : 0.0f;
		return mat;
	}
	static Matrix getZeroMatrix()
	{
		Matrix mat;
		for (int i = 0; i < 16; i++)
			mat.v[i] = 0.0f;
		return mat;
	}
	static Matrix getTranslationMatrix(const Vector3 &translation);
	static Matrix getRotationXMatrix(float radians);
	static Matrix getRotationYMatrix(float radians);
	static Matrix getRotationZMatrix(float radians);
	static Matrix getScaleMatrix(const Vector3 &scale);
	static Matrix getLHOrthoMatrix(float w, float h, float zn, float zf);
	static Matrix getLHPerspectiveMatrix(float fovy, float aspect, float zn, float zf);
	static Matrix getLHLookAtViewMatrix(const Vector3 &eye, const Vector3 &at, const Vector3 &up);
	static Matrix getRHOrthoMatrix(float w, float h, float zn, float zf);
	static Matrix getRHPerspectiveMatrix(float fovy, float aspect, float zn, float zf);
	static Matrix getRHLookAtViewMatrix(const Vector3 &eye, const Vector3 &at, const Vector3 &up);
	static Matrix multiplyMatrices(const Matrix &a, const Matrix &b);
};

// Three-dimensional vector
struct /*alignas(16)*/ Vector3
{
	union {
		struct { float x, y, z; }; // , w; // w used to align size.
		float coord[3];
	};
	constexpr Vector3() : x(0.0f), y(0.0f), z(0.0f) {}
	constexpr Vector3(float a, float b) : x(a), y(b), z(0.0f) {}
	constexpr Vector3(float a, float b, float c) : x(a), y(b), z(c) {}

	Vector3 operator+(const Vector3 &a) const { return Vector3(x + a.x, y + a.y, z + a.z); }
	Vector3 operator-(const Vector3 &a) const { return Vector3(x - a.x, y - a.y, z - a.z); }
	Vector3 operator*(const Vector3 &a) const { return Vector3(x * a.x, y * a.y, z * a.z); }
	Vector3 operator/(const Vector3 &a) const { return Vector3(x / a.x, y / a.y, z / a.z); }
	Vector3 operator+(float a) const { return Vector3(x + a, y + a, z + a); }
	Vector3 operator-(float a) const { return Vector3(x - a, y - a, z - a); }
	Vector3 operator*(float a) const { return Vector3(x * a, y * a, z * a); }
	Vector3 operator/(float a) const { return Vector3(x / a, y / a, z / a); }
	Vector3 operator-() const { return Vector3(-x, -y, -z); }

	Vector3 &operator+=(const Vector3 &a) { x += a.x; y += a.y; z += a.z; return *this; }
	Vector3 &operator-=(const Vector3 &a) { x -= a.x; y -= a.y; z -= a.z; return *this; }
	Vector3 &operator*=(const Vector3 &a) { x *= a.x; y *= a.y; z *= a.z; return *this; }
	Vector3 &operator/=(const Vector3 &a) { x /= a.x; y /= a.y; z /= a.z; return *this; }
	Vector3 &operator+=(float a) { x += a; y += a; z += a; return *this; }
	Vector3 &operator-=(float a) { x -= a; y -= a; z -= a; return *this; }
	Vector3 &operator*=(float a) { x *= a; y *= a; z *= a; return *this; }
	Vector3 &operator/=(float a) { x /= a; y /= a; z /= a; return *this; }

	bool operator==(const Vector3 &a) const { return (x == a.x) && (y == a.y) && (z == a.z); }
	bool operator!=(const Vector3 &a) const { return !((x == a.x) && (y == a.y) && (z == a.z)); }
	bool operator<(const Vector3 &a) const { return (x != a.x) ? (x < a.x) : ((y != a.y) ? (y < a.y) : (z < a.z)); }

	//void print() const {printf("(%f, %f, %f)\n", x, y, z);}
	float len2xy() const { return sqrt(x*x + y * y); }
	float sqlen2xy() const { return x * x + y * y; }
	float len2xz() const { return sqrt(x*x + z * z); }
	float sqlen2xz() const { return x * x + z * z; }
	float len3() const { return sqrt(x*x + y * y + z * z); }
	float sqlen3() const { return x * x + y * y + z * z; }
	Vector3 normal() const { float l = len3(); if (l != 0.0f) return Vector3(x / l, y / l, z / l); else return Vector3(0, 0, 0); }
	Vector3 normal2xz() const { float l = len2xz(); if (l != 0.0f) return Vector3(x / l, 0, z / l); else return Vector3(0, 0, 0); }
	float dot(const Vector3 &a) const { return a.x * x + a.y * y + a.z * z; }
	float dot2xz(const Vector3 &a) const { return a.x * x + a.z * z; }
	Vector3 cross(const Vector3 &v) const { return Vector3(y*v.z - z * v.y, z*v.x - x * v.z, x*v.y - y * v.x); }
	Vector3 transform(const Matrix &m) const;
	Vector3 transformNormal(const Matrix &m) const;
	Vector3 transformScreenCoords(const Matrix &m) const;

	float *begin() { return coord; }
	const float *begin() const { return coord; }
	float *end() { return coord + 3; }
	const float *end() const { return coord + 3; }
};
