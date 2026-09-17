#include "ImGuiPanelMesh.h"

#include <cstring>
#include <limits>

namespace flwnd {

static_assert(sizeof(ImDrawVert) == sizeof(float) * 5u,
              "XPLMPanelGraphics ImGui mesh layout must remain five floats");

bool rebuildImGuiPanelMesh(const ImDrawData* drawData,
                           void* fontTexture,
                           ImGuiPanelMesh& destination,
                           const PanelTextureResolver& resolveTexture) {
    destination.vertices.clear();
    destination.indices.clear();
    destination.drawCalls.clear();
    destination.valid = false;

    if (drawData == nullptr || drawData->CmdListsCount <= 0 || drawData->CmdLists == nullptr ||
        drawData->TotalVtxCount <= 0 || drawData->TotalIdxCount <= 0 ||
        fontTexture == nullptr) {
        destination.valid = true;
        return true;
    }

    const size_t vertexCount = static_cast<size_t>(drawData->TotalVtxCount);
    const size_t indexCount = static_cast<size_t>(drawData->TotalIdxCount);
    if (vertexCount > std::numeric_limits<size_t>::max() / 5u ||
        vertexCount > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        indexCount > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    if (destination.vertices.capacity() < vertexCount * 5u) {
        destination.vertices.reserve(vertexCount * 5u);
    }
    if (destination.indices.capacity() < indexCount) {
        destination.indices.reserve(indexCount);
    }

    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* commandList = drawData->CmdLists[listIndex];
        if (commandList == nullptr) {
            return false;
        }

        const size_t listVertexCount = static_cast<size_t>(commandList->VtxBuffer.Size);
        if (listVertexCount > std::numeric_limits<size_t>::max() / sizeof(ImDrawVert) ||
            listVertexCount > std::numeric_limits<size_t>::max() / 5u ||
            listVertexCount > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const size_t vertexBytes = listVertexCount * sizeof(ImDrawVert);
        const size_t oldFloatCount = destination.vertices.size();
        const size_t listFloatCount = listVertexCount * 5u;
        if ((listVertexCount > 0 && commandList->VtxBuffer.Data == nullptr) ||
            oldFloatCount > std::numeric_limits<size_t>::max() - listFloatCount ||
            oldFloatCount + listFloatCount >
                static_cast<size_t>(std::numeric_limits<int>::max()) * 5u) {
            return false;
        }
        const int vertexOffset = static_cast<int>(oldFloatCount / 5u);
        destination.vertices.resize(oldFloatCount + listFloatCount);
        std::memcpy(destination.vertices.data() + oldFloatCount,
                    commandList->VtxBuffer.Data, vertexBytes);

        if (commandList->IdxBuffer.Size < 0 ||
            (commandList->IdxBuffer.Size > 0 && commandList->IdxBuffer.Data == nullptr) ||
            static_cast<size_t>(commandList->IdxBuffer.Size) >
                std::numeric_limits<size_t>::max() - destination.indices.size() ||
            destination.indices.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        const int indexOffset = static_cast<int>(destination.indices.size());
        destination.indices.insert(destination.indices.end(), commandList->IdxBuffer.Data,
                                   commandList->IdxBuffer.Data + commandList->IdxBuffer.Size);

        for (int commandIndex = 0; commandIndex < commandList->CmdBuffer.Size; ++commandIndex) {
            const ImDrawCmd* command = &commandList->CmdBuffer[commandIndex];
            if (command->UserCallback != nullptr) {
                if (command->UserCallback != ImDrawCallback_ResetRenderState) {
                    command->UserCallback(commandList, command);
                }
                continue;
            }
            if (command->TextureId == nullptr || command->ElemCount == 0) {
                continue;
            }

            const size_t commandIndexOffset = static_cast<size_t>(command->IdxOffset);
            const size_t commandElementCount = static_cast<size_t>(command->ElemCount);
            if (commandIndexOffset > static_cast<size_t>(commandList->IdxBuffer.Size) ||
                commandElementCount > static_cast<size_t>(commandList->IdxBuffer.Size) - commandIndexOffset ||
                command->IdxOffset > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
                command->ElemCount > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
                command->VtxOffset > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
                return false;
            }

            for (size_t index = 0; index < commandElementCount; ++index) {
                const size_t sourceIndex = commandIndexOffset + index;
                const size_t sourceVertex = static_cast<size_t>(command->VtxOffset) +
                                            static_cast<size_t>(commandList->IdxBuffer.Data[sourceIndex]);
                if (sourceVertex >= listVertexCount) {
                    return false;
                }
            }

            void* texture = command->TextureId;
            if (texture != fontTexture) {
                texture = resolveTexture ? resolveTexture(texture) : nullptr;
                if (texture == nullptr) {
                    continue;
                }
            }

            flywithlua::panel::DrawCall drawCall;
            drawCall.texture = texture;
            drawCall.scissors[0] = command->ClipRect.x;
            drawCall.scissors[1] = command->ClipRect.y;
            drawCall.scissors[2] = command->ClipRect.z;
            drawCall.scissors[3] = command->ClipRect.w;
            drawCall.indexOffset = indexOffset + static_cast<int>(command->IdxOffset);
            drawCall.elementCount = static_cast<int>(command->ElemCount);
            if (static_cast<int>(command->VtxOffset) >
                std::numeric_limits<int>::max() - vertexOffset) {
                return false;
            }
            drawCall.vertexOffset = vertexOffset + static_cast<int>(command->VtxOffset);
            destination.drawCalls.push_back(drawCall);
        }
    }

    destination.valid = true;
    return true;
}

} // namespace flwnd
