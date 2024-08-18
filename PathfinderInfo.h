#pragma once

#include <array>
#include <string>
#include <vector>
#include "vecmat.h"

struct PfLeafNode
{
	int unk1;
	int unk2;
	float unk3;
	float unk4;
	float unk5;
	int unk6;
};

struct PfNode
{
	uint8_t unk1;
	uint8_t unk2;
	uint8_t unk3;
	int unk4;
	int unk5;
};

struct PfLayer
{
	int data;
};

struct PfWhat
{
	int unk1;
	float unk2;
};

struct PfDoor
{
	Vector3 position;
	float unk = 0.0f;

	std::array<uint8_t, 12> addData;
};

struct PfDoorsEdge
{
	float unk1;
	int unk2;
	int unk3;
};

struct PfRoom
{
	std::vector<PfLeafNode> leafNodes;
	std::vector<PfNode> nodes;
	std::vector<PfLayer> layers;
	std::vector<PfWhat> whats;
	Vector3 minCoords;
	Vector3 maxCoords;
	Vector3 altVec;

	std::vector<PfDoor> doors;
	std::vector<PfDoorsEdge> doorEdges;
	std::vector<Vector3> kongs;

	std::string unkString;
};

struct PfRoomInstance
{
	std::string name;
	int roomIndex;
	std::vector<int> doorInstanceIndices;
};

struct PfDoorInstance
{
	std::array<int, 4> roomIndices;
};

struct PfInfo
{
	std::vector<PfRoom> rooms;
	std::vector<PfRoomInstance> roomInstances;
	std::vector<PfDoorInstance> doorInstances;
	uint32_t lastValue;

	static PfInfo fromBytes(const void* data);
	std::vector<uint8_t> toBytes() const;
};