//
//     Copyright (C) Pixar. All rights reserved.
//
//     This license governs use of the accompanying software. If you
//     use the software, you accept this license. If you do not accept
//     the license, do not use the software.
//
//     1. Definitions
//     The terms "reproduce," "reproduction," "derivative works," and
//     "distribution" have the same meaning here as under U.S.
//     copyright law.  A "contribution" is the original software, or
//     any additions or changes to the software.
//     A "contributor" is any person or entity that distributes its
//     contribution under this license.
//     "Licensed patents" are a contributor's patent claims that read
//     directly on its contribution.
//
//     2. Grant of Rights
//     (A) Copyright Grant- Subject to the terms of this license,
//     including the license conditions and limitations in section 3,
//     each contributor grants you a non-exclusive, worldwide,
//     royalty-free copyright license to reproduce its contribution,
//     prepare derivative works of its contribution, and distribute
//     its contribution or any derivative works that you create.
//     (B) Patent Grant- Subject to the terms of this license,
//     including the license conditions and limitations in section 3,
//     each contributor grants you a non-exclusive, worldwide,
//     royalty-free license under its licensed patents to make, have
//     made, use, sell, offer for sale, import, and/or otherwise
//     dispose of its contribution in the software or derivative works
//     of the contribution in the software.
//
//     3. Conditions and Limitations
//     (A) No Trademark License- This license does not grant you
//     rights to use any contributor's name, logo, or trademarks.
//     (B) If you bring a patent claim against any contributor over
//     patents that you claim are infringed by the software, your
//     patent license from such contributor to the software ends
//     automatically.
//     (C) If you distribute any portion of the software, you must
//     retain all copyright, patent, trademark, and attribution
//     notices that are present in the software.
//     (D) If you distribute any portion of the software in source
//     code form, you may do so only under this license by including a
//     complete copy of this license with your distribution. If you
//     distribute any portion of the software in compiled or object
//     code form, you may only do so under a license that complies
//     with this license.
//     (E) The software is licensed "as-is." You bear the risk of
//     using it. The contributors give no express warranties,

#include "../far/dispatcher.h"
#include "../far/loopSubdivisionTables.h"
#include "../osd/d3d11DrawContext.h"

#include <D3D11.h>

#include <string.h>

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

OsdD3D11DrawContext::OsdD3D11DrawContext() :
    patchIndexBuffer(NULL),
    ptexCoordinateBuffer(NULL),
    ptexCoordinateBufferSRV(NULL),
    fvarDataBuffer(NULL),
    fvarDataBufferSRV(NULL),
    vertexBufferSRV(NULL),
    vertexValenceBuffer(NULL),
    vertexValenceBufferSRV(NULL),
    quadOffsetBuffer(NULL),
    quadOffsetBufferSRV(NULL),
    patchLevelBuffer(NULL),
    patchLevelBufferSRV(NULL)
{
}

OsdD3D11DrawContext::~OsdD3D11DrawContext()
{
    if (patchIndexBuffer) patchIndexBuffer->Release();
    if (ptexCoordinateBuffer) ptexCoordinateBuffer->Release();
    if (ptexCoordinateBufferSRV) ptexCoordinateBufferSRV->Release();
    if (fvarDataBuffer) fvarDataBuffer->Release();
    if (fvarDataBufferSRV) fvarDataBufferSRV->Release();
    if (vertexBufferSRV) vertexBufferSRV->Release();
    if (vertexValenceBuffer) vertexValenceBuffer->Release();
    if (vertexValenceBufferSRV) vertexValenceBufferSRV->Release();
    if (quadOffsetBuffer) quadOffsetBuffer->Release();
    if (quadOffsetBufferSRV) quadOffsetBufferSRV->Release();
    if (patchLevelBuffer) patchLevelBuffer->Release();
    if (patchLevelBufferSRV) patchLevelBufferSRV->Release();
};

bool
OsdD3D11DrawContext::allocate(FarMesh<OsdVertex> *farMesh,
              ID3D11Buffer *vertexBuffer,
              int numElements,
              ID3D11DeviceContext *pd3d11DeviceContext,
              bool requirePtexCoordinates,
              bool requireFVarData)
{
    ID3D11Device *pd3d11Device = NULL;
    pd3d11DeviceContext->GetDevice(&pd3d11Device);
    assert(pd3d11Device);

    FarPatchTables const * patchTables = farMesh->GetPatchTables();

    if (not patchTables) {
        // uniform patches
        isAdaptive = false;

        // XXX: farmesh should have FarDensePatchTable for dense mesh indices.
        //      instead of GetFaceVertices().
        const FarSubdivisionTables<OsdVertex> *tables = farMesh->GetSubdivisionTables();
        int level = tables->GetMaxLevel();
        const std::vector<int> &indices = farMesh->GetFaceVertices(level-1);

        // XXX: farmesh or FarSubdivisionTables should have a virtual method
        // to determine loop or not
        bool loop =
            dynamic_cast<const FarLoopSubdivisionTables<OsdVertex>*>(tables) != NULL;

        int numIndices = (int)indices.size();

        // Allocate and fill index buffer.
        D3D11_BUFFER_DESC bd;
        bd.ByteWidth = numIndices * sizeof(int);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = 0;
        bd.StructureByteStride = sizeof(int);
        D3D11_SUBRESOURCE_DATA initData;
        initData.pSysMem = &indices[0];
        HRESULT hr = pd3d11Device->CreateBuffer(&bd, &initData, &patchIndexBuffer);
        if (FAILED(hr)) {
            return false;
        }

        OsdPatchArray array;
        array.desc.type = kNonPatch;
        array.patchSize = loop ? 3 : 4;
        array.firstIndex = 0;
        array.numIndices = numIndices;

        patchArrays.push_back(array);
        return true;
    }

    // adaptive patches
    isAdaptive = true;

    // Determine buffer sizes
    int totalPatchIndices = 
        patchTables->GetFullRegularPatches().GetSize() +
        patchTables->GetFullBoundaryPatches().GetSize() +
        patchTables->GetFullCornerPatches().GetSize() +
        patchTables->GetFullGregoryPatches().GetSize() +
        patchTables->GetFullBoundaryGregoryPatches().GetSize();

    int totalPatchLevels =
        patchTables->GetFullRegularPatches().GetSize()/patchTables->GetRegularPatchRingsize() +
        patchTables->GetFullBoundaryPatches().GetSize()/patchTables->GetBoundaryPatchRingsize() +
        patchTables->GetFullCornerPatches().GetSize()/patchTables->GetCornerPatchRingsize() +
        patchTables->GetFullGregoryPatches().GetSize()/patchTables->GetGregoryPatchRingsize() +
        patchTables->GetFullBoundaryGregoryPatches().GetSize()/patchTables->GetGregoryPatchRingsize();

    for (int p=0; p<5; ++p) {
        totalPatchIndices +=
            patchTables->GetTransitionRegularPatches(p).GetSize();

        totalPatchLevels +=
            patchTables->GetTransitionRegularPatches(p).GetSize()/patchTables->GetRegularPatchRingsize();

        for (int r=0; r<4; ++r) {
            totalPatchIndices +=
                patchTables->GetTransitionBoundaryPatches(p, r).GetSize() +
                patchTables->GetTransitionCornerPatches(p, r).GetSize();

            totalPatchLevels +=
                patchTables->GetTransitionBoundaryPatches(p, r).GetSize()/patchTables->GetBoundaryPatchRingsize() +
                patchTables->GetTransitionCornerPatches(p, r).GetSize()/patchTables->GetCornerPatchRingsize();
        }
    }

    // Allocate and fill index buffer.
    D3D11_BUFFER_DESC bd;
    bd.ByteWidth = totalPatchIndices * sizeof(int);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = 0;
    bd.StructureByteStride = sizeof(int);
    HRESULT hr = pd3d11Device->CreateBuffer(&bd, NULL, &patchIndexBuffer);
    if (FAILED(hr)) {
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = pd3d11DeviceContext->Map(patchIndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr)) {
        return false;
    }
    unsigned int * indexBuffer = (unsigned int *) mappedResource.pData;

    bd.ByteWidth = totalPatchLevels * sizeof(unsigned int);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = 0;
    bd.StructureByteStride = sizeof(unsigned int);
    hr = pd3d11Device->CreateBuffer(&bd, NULL, &patchLevelBuffer);
    if (FAILED(hr)) {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
    ZeroMemory(&srvd, sizeof(srvd));
    srvd.Format = DXGI_FORMAT_R32_SINT;
    srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvd.Buffer.FirstElement = 0;
    srvd.Buffer.NumElements = totalPatchLevels;
    hr = pd3d11Device->CreateShaderResourceView(patchLevelBuffer, &srvd, &patchLevelBufferSRV);
    if (FAILED(hr)) {
        return false;
    }

    hr = pd3d11DeviceContext->Map(patchLevelBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr)) {
        return false;
    }
    unsigned int * levelBuffer = (unsigned int *) mappedResource.pData;

    int indexBase = 0;
    int levelBase = 0;
    int maxValence = patchTables->GetMaxValence();

    _AppendPatchArray(indexBuffer, &indexBase,
                        levelBuffer, &levelBase,
                        patchTables->GetFullRegularPatches(),
                        patchTables->GetRegularPatchRingsize(),
                        patchTables->GetFullRegularPtexCoordinates(),
                        patchTables->GetFullRegularFVarData(),
                        farMesh->GetTotalFVarWidth(),
                        OsdPatchDescriptor(kRegular, 0, 0, 0, 0), 0);
    _AppendPatchArray(indexBuffer, &indexBase,
                        levelBuffer, &levelBase,
                        patchTables->GetFullBoundaryPatches(),
                        patchTables->GetBoundaryPatchRingsize(),
                        patchTables->GetFullBoundaryPtexCoordinates(),
                        patchTables->GetFullBoundaryFVarData(),
                        farMesh->GetTotalFVarWidth(),
                        OsdPatchDescriptor(kBoundary, 0, 0, 0, 0), 0);
    _AppendPatchArray(indexBuffer, &indexBase,
                        levelBuffer, &levelBase,
                        patchTables->GetFullCornerPatches(),
                        patchTables->GetCornerPatchRingsize(),
                        patchTables->GetFullCornerPtexCoordinates(),
                        patchTables->GetFullCornerFVarData(),
                        farMesh->GetTotalFVarWidth(),
                        OsdPatchDescriptor(kCorner, 0, 0, 0, 0), 0);
    _AppendPatchArray(indexBuffer, &indexBase,
                        levelBuffer, &levelBase,
                        patchTables->GetFullGregoryPatches(),
                        patchTables->GetGregoryPatchRingsize(),
                        patchTables->GetFullGregoryPtexCoordinates(),
                        patchTables->GetFullGregoryFVarData(),
                        farMesh->GetTotalFVarWidth(),
                        OsdPatchDescriptor(kGregory, 0, 0,
                                           maxValence, numElements), 0);
    _AppendPatchArray(indexBuffer, &indexBase,
                        levelBuffer, &levelBase,
                        patchTables->GetFullBoundaryGregoryPatches(),
                        patchTables->GetGregoryPatchRingsize(),
                        patchTables->GetFullBoundaryGregoryPtexCoordinates(),
                        patchTables->GetFullBoundaryGregoryFVarData(),
                        farMesh->GetTotalFVarWidth(),
                        OsdPatchDescriptor(kBoundaryGregory, 0, 0,
                                           maxValence, numElements),
                        (int)patchTables->GetFullGregoryPatches().GetSize());

    for (int p=0; p<5; ++p) {
        _AppendPatchArray(indexBuffer, &indexBase,
                        levelBuffer, &levelBase,
                        patchTables->GetTransitionRegularPatches(p),
                        patchTables->GetRegularPatchRingsize(),
                        patchTables->GetTransitionRegularPtexCoordinates(p),
                        patchTables->GetTransitionRegularFVarData(p),
                        farMesh->GetTotalFVarWidth(),
                        OsdPatchDescriptor(kTransitionRegular, p, 0, 0, 0), 0);
        for (int r=0; r<4; ++r) {
            _AppendPatchArray(indexBuffer, &indexBase,
                        levelBuffer, &levelBase,
                        patchTables->GetTransitionBoundaryPatches(p, r),
                        patchTables->GetBoundaryPatchRingsize(),
                        patchTables->GetTransitionBoundaryPtexCoordinates(p, r),
                        patchTables->GetTransitionBoundaryFVarData(p, r),
                        farMesh->GetTotalFVarWidth(),
                        OsdPatchDescriptor(kTransitionBoundary, p, r, 0, 0), 0);
            _AppendPatchArray(indexBuffer, &indexBase,
                        levelBuffer, &levelBase,
                        patchTables->GetTransitionCornerPatches(p, r),
                        patchTables->GetCornerPatchRingsize(),
                        patchTables->GetTransitionCornerPtexCoordinates(p, r),
                        patchTables->GetTransitionCornerFVarData(p, r),
                        farMesh->GetTotalFVarWidth(),
                        OsdPatchDescriptor(kTransitionCorner, p, r, 0, 0), 0);
        }
    }

    pd3d11DeviceContext->Unmap(patchIndexBuffer, 0);
    pd3d11DeviceContext->Unmap(patchLevelBuffer, 0);

    // allocate and initialize additional buffer data
    FarPatchTables::VertexValenceTable const &
        valenceTable = patchTables->GetVertexValenceTable();

    if (not valenceTable.empty()) {
        D3D11_BUFFER_DESC bd;
        bd.ByteWidth = UINT(valenceTable.size() * sizeof(unsigned int));
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = 0;
        bd.StructureByteStride = sizeof(unsigned int);
        D3D11_SUBRESOURCE_DATA initData;
        initData.pSysMem = &valenceTable[0];
        HRESULT hr = pd3d11Device->CreateBuffer(&bd, &initData, &vertexValenceBuffer);
        if (FAILED(hr)) {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
        ZeroMemory(&srvd, sizeof(srvd));
        srvd.Format = DXGI_FORMAT_R32_SINT;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvd.Buffer.FirstElement = 0;
        srvd.Buffer.NumElements = UINT(valenceTable.size());
        hr = pd3d11Device->CreateShaderResourceView(vertexValenceBuffer, &srvd, &vertexValenceBufferSRV);
        if (FAILED(hr)) {
            return false;
        }

        ZeroMemory(&srvd, sizeof(srvd));
        srvd.Format = DXGI_FORMAT_R32_FLOAT;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvd.Buffer.FirstElement = 0;
        srvd.Buffer.NumElements = 6 * farMesh->GetNumVertices(); // XXX: dyu
        hr = pd3d11Device->CreateShaderResourceView(vertexBuffer, &srvd, &vertexBufferSRV);
        if (FAILED(hr)) {
            return false;
        }
    }

    FarPatchTables::QuadOffsetTable const &
        quadOffsetTable = patchTables->GetQuadOffsetTable();

    if (not quadOffsetTable.empty()) {
        D3D11_BUFFER_DESC bd;
        bd.ByteWidth = UINT(quadOffsetTable.size() * sizeof(unsigned int));
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = 0;
        bd.MiscFlags = 0;
        bd.StructureByteStride = sizeof(unsigned int);
        D3D11_SUBRESOURCE_DATA initData;
        initData.pSysMem = &quadOffsetTable[0];
        HRESULT hr = pd3d11Device->CreateBuffer(&bd, &initData, &quadOffsetBuffer);
        if (FAILED(hr)) {
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvd;
        ZeroMemory(&srvd, sizeof(srvd));
        srvd.Format = DXGI_FORMAT_R32_SINT;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvd.Buffer.FirstElement = 0;
        srvd.Buffer.NumElements = UINT(quadOffsetTable.size());
        hr = pd3d11Device->CreateShaderResourceView(quadOffsetBuffer, &srvd, &quadOffsetBufferSRV);
        if (FAILED(hr)) {
            return false;
        }
    }

    return true;
}

void
OsdD3D11DrawContext::_AppendPatchArray(
        unsigned int *indexBuffer, int *indexBase,
        unsigned int *levelBuffer, int *levelBase,
        FarPatchTables::PTable const & ptable, int patchSize,
        FarPatchTables::PtexCoordinateTable const & ptexTable,
        FarPatchTables::FVarDataTable const & fvarTable, int fvarDataWidth,
        OsdPatchDescriptor const & desc,
        int gregoryQuadOffsetBase)
{
    if (ptable.IsEmpty()) {
        return;
    } 

    OsdPatchArray array;
    array.desc = desc;
    array.patchSize = patchSize;
    array.firstIndex = *indexBase;
    array.numIndices = ptable.GetSize();
    array.levelBase = *levelBase;
    array.gregoryQuadOffsetBase = gregoryQuadOffsetBase;

    int numSubPatches = 1;
    if (desc.type == OpenSubdiv::kTransitionRegular or
        desc.type == OpenSubdiv::kTransitionBoundary or
        desc.type == OpenSubdiv::kTransitionCorner) {
        int subPatchCounts[] = { 3, 4, 4, 4, 2 };
        numSubPatches = subPatchCounts[desc.pattern];
    }

    for (int subpatch = 0; subpatch < numSubPatches; ++subpatch) {
        array.desc.subpatch = subpatch;
        patchArrays.push_back(array);
    }

    memcpy(indexBuffer + array.firstIndex,
           ptable[0], array.numIndices * sizeof(unsigned int));
    *indexBase += array.numIndices;

    std::vector<unsigned int> levels;
    levels.reserve(ptable.GetSize());
    for (int i = 0; i < (int) ptable.GetMarkers().size()-1; ++i) {
        int numPrims = ptable.GetNumElements(i)/array.patchSize;
        for (int j = 0; j < numPrims; ++j) {
            levels.push_back(i);
        }
    }

    memcpy(levelBuffer + array.levelBase,
           &levels[0], levels.size() * sizeof(unsigned int));
    *levelBase += (int)levels.size();
}

} // end namespace OPENSUBDIV_VERSION
} // end namespace OpenSubdiv
