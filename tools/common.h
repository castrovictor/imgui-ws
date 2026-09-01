/*! \file common.h
 *  \brief Saving/loading Dear ImGui DrawData
 */

#pragma once

#include "imgui/imgui.h"
#include "imgui-ws/imgui-ws.h"

#include <vector>
#include <fstream>
#include <cstring>

namespace {

// helper functions to serialize/unserialize Dear ImGui data

template<typename T>
    inline void serialize(const T & t, std::vector<char> & buf) {
        std::copy((char *)(&t), (char *)(&t) + sizeof(T), std::back_inserter(buf));
    }

template<>
    inline void serialize<ImDrawCmd>(const ImDrawCmd & t, std::vector<char> & buf) {
        serialize(t.ElemCount, buf);
        serialize(t.ClipRect.x, buf);
        serialize(t.ClipRect.y, buf);
        serialize(t.ClipRect.z, buf);
        serialize(t.ClipRect.w, buf);
        serialize(t.GetTexID(), buf);
        serialize(t.VtxOffset, buf);
        serialize(t.IdxOffset, buf);
    }

template<typename T>
    inline void serialize(const ImVector<T> & t, std::vector<char> & buf) {
        uint32_t n = t.Size;
        serialize(n, buf);
        std::copy((char *)(t.Data), (char *)(t.Data) + n*sizeof(T), std::back_inserter(buf));
    }

template<>
    inline void serialize<ImDrawCmd>(const ImVector<ImDrawCmd> & t, std::vector<char> & buf) {
        uint32_t n = t.Size;
        serialize(n, buf);
        for (uint32_t i = 0; i < n; ++i) {
            serialize(t[i], buf);
        }
    }

template<typename T>
    inline void unserialize(T & t, const std::vector<char> & buf, size_t & offset) {
        std::memcpy((char *)(&t), buf.data() + offset, sizeof(T));
        offset += sizeof(T);
    }

    template<>
    inline void unserialize<ImDrawCmd>(ImDrawCmd & t,
                                       const std::vector<char> & buf,
                                       size_t & offset)
    {
        // 1️⃣  ElemCount
        unserialize(t.ElemCount, buf, offset);
    
        // 2️⃣  ClipRect (still ImVec4)
        unserialize(t.ClipRect.x, buf, offset);
        unserialize(t.ClipRect.y, buf, offset);
        unserialize(t.ClipRect.z, buf, offset);
        unserialize(t.ClipRect.w, buf, offset);
    
        // 3️⃣  Texture ID – new field is inside TexRef
        ImTextureID texId = 0;
        unserialize(texId, buf, offset);
        t.TexRef._TexID   = texId;      // <-- **new name**
        t.TexRef._TexData = nullptr;    // clear optional pointer
    
        // 4️⃣  Offsets
        unserialize(t.VtxOffset, buf, offset);
        unserialize(t.IdxOffset, buf, offset);
    
        // 5️⃣  Callbacks – keep them null (as the original code forced)
        t.UserCallback        = nullptr;
        t.UserCallbackData    = nullptr;
        t.UserCallbackDataSize = 0;
        t.UserCallbackDataOffset = -1;
    }

template<typename T>
    inline void unserialize(ImVector<T> & t, std::vector<char> & buf, size_t & offset) {
        uint32_t n = 0;
        unserialize(n, buf, offset);
        t.resize(n);
        std::memcpy((char *)(t.Data), buf.data() + offset, n*sizeof(T));
        offset += n*sizeof(T);
    }

template<>
    inline void unserialize<ImDrawCmd>(ImVector<ImDrawCmd> & t, std::vector<char> & buf, size_t & offset) {
        uint32_t n = 0;
        unserialize(n, buf, offset);
        t.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            unserialize(t[i], buf, offset);
        }
    }

}

struct Session {
    constexpr static auto kHeader = "Dear ImGui DrawData v1.0";

    using FrameData = std::vector<char>;

    bool save(const char * fname) const {
        std::ofstream fs(fname, std::ios::binary);

        fs.write(kHeader, strlen(kHeader));

        uint32_t nFrames = frames.size();
        fs.write((char *)(&nFrames), sizeof(nFrames));

        for (uint32_t i = 0; i < nFrames; ++i) {
            uint32_t frameSize = frames[i].size();
            fs.write((char *)(&frameSize), sizeof(frameSize));
            fs.write((char *)(frames[i].data()), frames[i].size());
        }

        return true;
    }

    bool load(const char * fname) {
        std::ifstream fs(fname, std::ios::binary);
        char header[64];
        std::fill(header, header + 64, 0);
        fs.read(header, strlen(kHeader));
        if (strcmp(header, kHeader)) {
            return false;
        }

        uint32_t nFrames = 0;
        fs.read((char *)(&nFrames), sizeof(nFrames));

        frames.resize(nFrames);
        for (uint32_t i = 0; i < nFrames; ++i) {
            uint32_t frameSize = 0;
            fs.read((char *)(&frameSize), sizeof(frameSize));

            frames[i].resize(frameSize);
            fs.read((char *)(frames[i].data()), frames[i].size());
        }

        return true;
    }

    bool addFrame(const ImDrawData * drawData) {
        FrameData frame;
    
        serialize(drawData->Valid,               frame);
        serialize(drawData->TotalIdxCount,       frame);
        serialize(drawData->TotalVtxCount,       frame);
        serialize(drawData->DisplayPos.x,        frame);
        serialize(drawData->DisplayPos.y,        frame);
        serialize(drawData->DisplaySize.x,       frame);
        serialize(drawData->DisplaySize.y,       frame);
        serialize(drawData->FramebufferScale.x,  frame);
        serialize(drawData->FramebufferScale.y,  frame);
    
        // ---- NEW: number of command‑lists (ImVector size) ----
        uint32_t nCmdLists = static_cast<uint32_t>(drawData->CmdLists.Size);
        serialize(nCmdLists, frame);
    
        for (int32_t iList = 0; iList < nCmdLists; ++iList) {
            ImDrawList* cmdList = drawData->CmdLists[iList];   // <-- note the pointer
            serialize(cmdList->CmdBuffer, frame);
            serialize(cmdList->VtxBuffer, frame);
            serialize(cmdList->IdxBuffer, frame);
            serialize(cmdList->Flags,     frame);
        }
    
        frames.emplace_back(std::move(frame));
        return true;
    }


    bool getFrame(int32_t fid,
        ImDrawData * drawData,
        std::vector<ImDrawList> & drawLists,
        ImDrawListSharedData * drawListSharedData)   // ← can be const or non‑const; see previous answer
    {
        if (fid >= static_cast<int32_t>(frames.size())) return false;

        size_t   offset = 0;
        auto &   buf    = frames[fid];

        unserialize(drawData->Valid,               buf, offset);
        unserialize(drawData->TotalIdxCount,       buf, offset);
        unserialize(drawData->TotalVtxCount,       buf, offset);
        unserialize(drawData->DisplayPos.x,        buf, offset);
        unserialize(drawData->DisplayPos.y,        buf, offset);
        unserialize(drawData->DisplaySize.x,       buf, offset);
        unserialize(drawData->DisplaySize.y,       buf, offset);
        unserialize(drawData->FramebufferScale.x,  buf, offset);
        unserialize(drawData->FramebufferScale.y,  buf, offset);

        // ---- NEW: read the number of command‑lists ----
        uint32_t nCmdLists = 0;
        unserialize(nCmdLists, buf, offset);

        // Make sure we have enough *ImDrawList* objects to point at
        if (static_cast<int>(drawLists.size()) < static_cast<int>(nCmdLists))
        drawLists.resize(nCmdLists, ImDrawList(drawListSharedData));

        // Resize the ImVector that lives inside ImDrawData
        drawData->CmdLists.resize(nCmdLists);

        // Fill the vector with pointers to the pre‑allocated ImDrawList objects
        for (int32_t iList = 0; iList < static_cast<int32_t>(nCmdLists); ++iList) {
            drawData->CmdLists[iList] = &drawLists[iList];

            ImDrawList* cmdList = drawData->CmdLists[iList];
            unserialize(cmdList->CmdBuffer, buf, offset);
            unserialize(cmdList->VtxBuffer, buf, offset);
            unserialize(cmdList->IdxBuffer, buf, offset);
            unserialize(cmdList->Flags,     buf, offset);
        }

        return true;
    }

    int32_t nFrames() const {
        return frames.size();
    }

    uint64_t totalSize_bytes() const {
        uint64_t result = 0;

        for (auto & frame : frames) {
            result += frame.size();
        }

        return result;
    }

    void printInfo() const {
        printf("    - Total frames      = %d\n", (int) frames.size());
        printf("    - Total size        = %d bytes\n", (int) totalSize_bytes());
    }

    std::vector<FrameData> frames;
};
