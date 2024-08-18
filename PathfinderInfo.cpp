#include "PathfinderInfo.h"
#include "ByteReader.h"

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
            reader.readTo(leafNode.unk1, leafNode.unk2, leafNode.unk3, leafNode.unk4, leafNode.unk5, leafNode.unk6);
        }
        room.nodes.resize(reader.readUint32());
        for (auto& node : room.nodes) {
            reader.readTo(node.unk1, node.unk2, node.unk3, node.unk4, node.unk5);
        }
        room.layers.resize(reader.readUint32());
        for (auto& layer : room.layers) {
            reader.readTo(layer.data);
        }
        room.whats.resize(reader.readUint32());
        for (auto& what : room.whats) {
            reader.readTo(what.unk1);
        }
        for (auto& what : room.whats) {
            reader.readTo(what.unk2);
        }
        reader.readTo(room.minCoords);
        reader.readTo(room.maxCoords);
        reader.readTo(room.altVec);

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
