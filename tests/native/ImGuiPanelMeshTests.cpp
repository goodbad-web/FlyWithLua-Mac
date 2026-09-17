#include "ImGuiPanelMesh.h"

#include "imgui.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {

int gUserCallbackCount = 0;

void countUserCallback(const ImDrawList*, const ImDrawCmd*) {
    ++gUserCallbackCount;
}

void assertDrawCallsAreInBounds(const flwnd::ImGuiPanelMesh& mesh) {
    const std::size_t vertexCount = mesh.vertices.size() / 5u;
    for (const auto& call : mesh.drawCalls) {
        assert(call.indexOffset >= 0);
        assert(call.elementCount >= 0);
        assert(call.vertexOffset >= 0);
        assert(static_cast<std::size_t>(call.indexOffset) +
                   static_cast<std::size_t>(call.elementCount) <= mesh.indices.size());
        for (int index = 0; index < call.elementCount; ++index) {
            const std::size_t indexPosition = static_cast<std::size_t>(call.indexOffset + index);
            const std::size_t vertexPosition = static_cast<std::size_t>(call.vertexOffset) +
                                               mesh.indices[indexPosition];
            assert(vertexPosition < vertexCount);
        }
    }
}

} // namespace

int main() {
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(640.0f, 480.0f);

    static int fontTexture;
    static int legacyTexture;
    static int resolvedTexture;
    io.Fonts->TexID = &fontTexture;

    ImDrawList firstList(ImGui::GetDrawListSharedData());
    ImDrawList secondList(ImGui::GetDrawListSharedData());
    firstList._ResetForNewFrame();
    firstList.PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(640.0f, 480.0f));
    firstList.PushTextureID(&fontTexture);
    firstList.AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(20.0f, 20.0f), IM_COL32_WHITE);
    firstList.PopTextureID();
    firstList.PushTextureID(&legacyTexture);
    firstList.AddImage(&legacyTexture, ImVec2(20.0f, 20.0f), ImVec2(52.0f, 52.0f));
    firstList.AddCallback(countUserCallback, nullptr);
    firstList.AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    firstList.PopTextureID();
    secondList._ResetForNewFrame();
    secondList.PushClipRect(ImVec2(10.0f, 10.0f), ImVec2(320.0f, 240.0f));
    secondList.PushTextureID(&fontTexture);
    secondList.AddRectFilled(ImVec2(40.0f, 40.0f), ImVec2(80.0f, 80.0f), IM_COL32_WHITE);
    secondList.PopTextureID();
    ImDrawList* drawLists[] = {&firstList, &secondList};
    ImDrawData drawDataStorage;
    drawDataStorage.Valid = true;
    drawDataStorage.CmdListsCount = 2;
    drawDataStorage.CmdLists = drawLists;
    drawDataStorage.TotalVtxCount = firstList.VtxBuffer.Size + secondList.VtxBuffer.Size;
    drawDataStorage.TotalIdxCount = firstList.IdxBuffer.Size + secondList.IdxBuffer.Size;
    drawDataStorage.DisplaySize = io.DisplaySize;
    drawDataStorage.FramebufferScale = ImVec2(1.0f, 1.0f);
    const ImDrawData* drawData = &drawDataStorage;

    flwnd::ImGuiPanelMesh mesh;
    const bool rebuilt = flwnd::rebuildImGuiPanelMesh(
        drawData, &fontTexture, mesh,
        [](void* texture) -> void* {
            return texture == &legacyTexture ? &resolvedTexture : nullptr;
        });
    assert(rebuilt);
    assert(mesh.valid);
    assert(!mesh.vertices.empty());
    assert(!mesh.indices.empty());
    assert(!mesh.drawCalls.empty());
    assert(gUserCallbackCount == 1);
    assertDrawCallsAreInBounds(mesh);

    bool sawResolvedTexture = false;
    for (const auto& call : mesh.drawCalls) {
        sawResolvedTexture |= call.texture == &resolvedTexture;
    }
    assert(sawResolvedTexture);

    const float* vertexData = mesh.vertices.data();
    const std::uint16_t* indexData = mesh.indices.data();
    const auto* drawCallData = mesh.drawCalls.data();
    const std::size_t vertexCapacity = mesh.vertices.capacity();
    const std::size_t indexCapacity = mesh.indices.capacity();
    const std::size_t drawCallCapacity = mesh.drawCalls.capacity();

    gUserCallbackCount = 0;
    const bool rebuiltAgain = flwnd::rebuildImGuiPanelMesh(
        drawData, &fontTexture, mesh,
        [](void* texture) -> void* {
            return texture == &legacyTexture ? &resolvedTexture : nullptr;
        });
    assert(rebuiltAgain);
    assert(mesh.valid);
    assert(gUserCallbackCount == 1);
    assert(mesh.vertices.data() == vertexData);
    assert(mesh.indices.data() == indexData);
    assert(mesh.drawCalls.data() == drawCallData);
    assert(mesh.vertices.capacity() == vertexCapacity);
    assert(mesh.indices.capacity() == indexCapacity);
    assert(mesh.drawCalls.capacity() == drawCallCapacity);

    ImDrawList* invalidList = nullptr;
    ImDrawCmd* invalidCommand = nullptr;
    for (int listIndex = 0; listIndex < drawData->CmdListsCount && invalidCommand == nullptr;
         ++listIndex) {
        ImDrawList* list = drawData->CmdLists[listIndex];
        for (int commandIndex = 0; commandIndex < list->CmdBuffer.Size; ++commandIndex) {
            if (list->CmdBuffer[commandIndex].UserCallback == nullptr &&
                list->CmdBuffer[commandIndex].ElemCount > 0) {
                invalidList = list;
                invalidCommand = &list->CmdBuffer[commandIndex];
                break;
            }
        }
    }
    assert(invalidCommand != nullptr);
    const unsigned int oldIdxOffset = invalidCommand->IdxOffset;
    invalidCommand->IdxOffset = static_cast<unsigned int>(
        invalidList->IdxBuffer.Size + 1);
    assert(!flwnd::rebuildImGuiPanelMesh(drawData, &fontTexture, mesh,
                                         [](void*) -> void* { return &resolvedTexture; }));
    invalidCommand->IdxOffset = oldIdxOffset;

    ImGui::DestroyContext(context);
    return 0;
}
