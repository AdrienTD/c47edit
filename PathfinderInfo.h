#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "vecmat.h"

struct PfLeafNode
{
	int edgeCount = 0;
	int firstEdgeIndex = 0;
	float centerX = 0.0f;
	float centerZ = 0.0f;
	float centerY = 0.0f;
	int unk6 = 0;
};

struct PfNode
{
	uint16_t value = 0;
	uint8_t comparison = 0;
	int leftNodeIndex = 0;
	int rightNodeIndex = 0;
};

struct PfLayer
{
	int startNodeIndex = 0;
};

struct PfLeafEdge
{
	int neighborLeafNodeIndex;
	float cost;
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
	std::vector<PfLeafEdge> leafEdges;
	Vector3 minCoords;
	Vector3 maxCoords;
	Vector3 resolution;

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