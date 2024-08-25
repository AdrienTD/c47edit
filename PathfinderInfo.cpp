#include "PathfinderInfo.h"
#include "ByteReader.h"
#include "ByteWriter.h"

PfInfo PfInfo::fromBytes(const void* data)
{
    ByteReader reader(data);
    PfInfo info;

    uint32_t nameBufferSize = reader.readUint32();
    if (nameBufferSize == 0x12312312)
        nameBufferSize = reader.readInt32();

    const char* nameBuffer = reinterpret_cast<const char*>(reader.currentPointer());
    reader.skip(nameBufferSize);

    info.rooms.resize(reader.readUint32());
    for (auto& room : info.rooms) {
        room.leafNodes.resize(reader.readUint32());
        for (auto& leafNode : room.leafNodes) {
            reader.readTo(leafNode.edgeCount, leafNode.firstEdgeIndex, leafNode.centerX, leafNode.centerZ, leafNode.centerY, leafNode.unk6);
        }
        room.nodes.resize(reader.readUint32());
        for (auto& node : room.nodes) {
            reader.readTo(node.value, node.comparison, node.leftNodeIndex, node.rightNodeIndex);
        }
        room.layers.resize(reader.readUint32());
        for (auto& layer : room.layers) {
            reader.readTo(layer.startNodeIndex);
        }
        room.leafEdges.resize(reader.readUint32());
        for (auto& what : room.leafEdges) {
            reader.readTo(what.neighborLeafNodeIndex);
        }
        for (auto& what : room.leafEdges) {
            reader.readTo(what.cost);
        }
        reader.readTo(room.minCoords);
        reader.readTo(room.maxCoords);
        reader.readTo(room.resolution);

        room.doors.resize(reader.readUint32());
        for (auto& door : room.doors) {
            reader.readTo(door.position, door.unk);
        }

        room.doorEdges.resize(room.doors.size() * (room.doors.size() - 1) / 2);
        for (auto& edge : room.doorEdges) {
            reader.readTo(edge.unk1, edge.unk2, edge.unk3);
        }

        for (auto& door : room.doors) {
            for (auto& x : door.addData)
                reader.readTo(x);
        }

        room.kongs.resize(reader.readUint32());
        for (auto& kong : room.kongs) {
            reader.readTo(kong);
        }

        uint32_t stringOffset = reader.readUint32();
        room.unkString = nameBuffer + stringOffset;
    }

    info.roomInstances.resize(reader.readUint32());
    for (auto& roomInst : info.roomInstances) {
        uint32_t stringOffset = reader.readUint32();
        roomInst.name = nameBuffer + stringOffset;
        reader.readTo(roomInst.roomIndex);
        roomInst.doorInstanceIndices.resize(info.rooms.at(roomInst.roomIndex).doors.size());
        for (auto& x : roomInst.doorInstanceIndices)
            reader.readTo(x);
    }

    info.doorInstances.resize(reader.readUint32());
    for (auto& doorInst : info.doorInstances) {
        for (auto& x : doorInst.roomIndices)
            reader.readTo(x);
    }

    reader.readTo(info.lastValue);

    return info;
}

std::vector<uint8_t> PfInfo::toBytes() const
{
    ByteWriter<std::vector<uint8_t>> writer;

    writer.addU32(0x12312312);

    ByteWriter<std::vector<uint8_t>> nameBuffer;
    std::vector<int> roomNameOffsets(rooms.size(), 0);
    std::vector<int> roomInstNameOffsets(rooms.size(), 0);
    for (size_t i = 0; i < rooms.size(); ++i) {
        auto& room = rooms[i];
        roomNameOffsets[i] = (int)nameBuffer.size();
        nameBuffer.addStringNT(room.unkString);
    }
    for (size_t i = 0; i < roomInstances.size(); ++i) {
        auto& inst = roomInstances[i];
        roomInstNameOffsets[i] = (int)nameBuffer.size();
        nameBuffer.addStringNT(inst.name);
    }

    writer.addU32(nameBuffer.size());
    writer.addData(nameBuffer.getPointer(), nameBuffer.size());

    writer.addU32(rooms.size());
    for (size_t r = 0; r < rooms.size(); ++r) {
        auto& room = rooms[r];
        writer.addU32(room.leafNodes.size());
        for (auto& leafNode : room.leafNodes) {
            writer.addValues(leafNode.edgeCount, leafNode.firstEdgeIndex, leafNode.centerX, leafNode.centerZ, leafNode.centerY, leafNode.unk6);
        }
        writer.addU32(room.nodes.size());
        for (auto& node : room.nodes) {
            writer.addValues(node.value, node.comparison, node.leftNodeIndex, node.rightNodeIndex);
        }
        writer.addU32(room.layers.size());
        for (auto& layer : room.layers) {
            writer.addValues(layer.startNodeIndex);
        }
        writer.addU32(room.leafEdges.size());
        for (auto& what : room.leafEdges) {
            writer.addValues(what.neighborLeafNodeIndex);
        }
        for (auto& what : room.leafEdges) {
            writer.addValues(what.cost);
        }
        writer.addValue(room.minCoords);
        writer.addValue(room.maxCoords);
        writer.addValue(room.resolution);

        writer.addU32(room.doors.size());
        for (auto& door : room.doors) {
            writer.addValues(door.position, door.unk);
        }

        for (auto& edge : room.doorEdges) {
            writer.addValues(edge.unk1, edge.unk2, edge.unk3);
        }

        for (auto& door : room.doors) {
            for (auto& x : door.addData)
                writer.addValues(x);
        }

        writer.addU32(room.kongs.size());
        for (auto& kong : room.kongs) {
            writer.addValues(kong);
        }

        writer.addU32(roomNameOffsets[r]);
    }

    writer.addU32(roomInstances.size());
    for (size_t i = 0; i < roomInstances.size(); ++i) {
        auto& roomInst = roomInstances[i];
        writer.addU32(roomInstNameOffsets[i]);
        writer.addValue(roomInst.roomIndex);
        for (auto& x : roomInst.doorInstanceIndices)
            writer.addValue(x);
    }

    writer.addU32(doorInstances.size());
    for (auto& doorInst : doorInstances) {
        for (auto& x : doorInst.roomIndices)
            writer.addValue(x);
    }

    writer.addValue(lastValue);

    return writer.take();
}
