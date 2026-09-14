#include "GpuSimulator.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

// Direct3D 11 コンピュートシェーダー (HLSL)
// セルラー・オートマトン式 墨汁浸透・水分毛管流・乾燥シミュレーション
const char* const kPropagationCS = R"(
cbuffer SimParams : register(b0)
{
    uint g_width;
    uint g_height;
    int  g_threshold;     // 350
    int  g_amount;        // 40
    int  g_wetLoss;       // 40
    int  g_wetDryRate;    // 1
    int  g_wetThreshold;  // 8
    int  g_maxInk;        // 450
};

Texture2D<int>    g_inkIn    : register(t0);
Texture2D<uint>   g_wetIn    : register(t1);
RWTexture2D<int>   g_inkOut   : register(u0);
RWTexture2D<uint>  g_wetOut   : register(u1);
RWTexture2D<unorm float4> g_pixelOut : register(u2);

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= g_width || DTid.y >= g_height) return;
    int2 pos = int2(DTid.xy);

    int curInk = g_inkIn.Load(int3(pos, 0));
    uint curWet = g_wetIn.Load(int3(pos, 0));

    // 1. 水分乾燥
    uint newWet = (curWet > (uint)g_wetDryRate) ? (curWet - (uint)g_wetDryRate) : 0u;

    int deltaInk = 0;
    const int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };

    // 2. 自身からの流出判定
    if (curInk > g_threshold && (int)curWet > g_wetThreshold)
    {
        int count = 0;
        [unroll]
        for (int j = 0; j < 4; ++j)
        {
            int2 npos = pos + offsets[j];
            if (npos.x >= 0 && npos.x < (int)g_width && npos.y >= 0 && npos.y < (int)g_height)
            {
                int nInk = g_inkIn.Load(int3(npos, 0));
                if (curInk > nInk)
                {
                    count++;
                }
            }
        }

        if (count > 0)
        {
            deltaInk -= g_amount;
        }
        else
        {
            deltaInk -= 10;
        }
    }

    // 3. 周囲からの流入判定
    [unroll]
    for (int k = 0; k < 4; ++k)
    {
        int2 npos = pos + offsets[k];
        if (npos.x >= 0 && npos.x < (int)g_width && npos.y >= 0 && npos.y < (int)g_height)
        {
            int nInk = g_inkIn.Load(int3(npos, 0));
            uint nWet = g_wetIn.Load(int3(npos, 0));

            if (nInk > g_threshold && (int)nWet > g_wetThreshold && nInk > curInk)
            {
                // 近傍セルの流出先数
                int nCount = 0;
                [unroll]
                for (int m = 0; m < 4; ++m)
                {
                    int2 nnpos = npos + offsets[m];
                    if (nnpos.x >= 0 && nnpos.x < (int)g_width && nnpos.y >= 0 && nnpos.y < (int)g_height)
                    {
                        if (nInk > g_inkIn.Load(int3(nnpos, 0)))
                        {
                            nCount++;
                        }
                    }
                }

                if (nCount > 0)
                {
                    int flowIn = max(1, g_amount / nCount);
                    deltaInk += flowIn;

                    int carriedWet = (int)nWet - g_wetLoss;
                    if (carriedWet > (int)newWet)
                    {
                        newWet = (uint)carriedWet;
                    }
                }
            }
        }
    }

    int finalInk = clamp(curInk + deltaInk, 0, g_maxInk);
    g_inkOut[pos] = finalInk;
    g_wetOut[pos] = newWet;

    // 4. カラー算出 (白〜黒の ARGB ピクセル)
    float inkRatio = saturate((float)finalInk / 255.0f);
    float c = 1.0f - inkRatio;
    g_pixelOut[pos] = float4(c, c, c, 1.0f);
}
)";

} // namespace

GpuSimulator::GpuSimulator()
{
}

GpuSimulator::~GpuSimulator()
{
    Release();
}

void GpuSimulator::Release()
{
    m_available = false;
    m_computeShader.Reset();
    m_constantBuffer.Reset();
    for (int i = 0; i < 2; ++i)
    {
        m_uavInk[i].Reset();
        m_srvInk[i].Reset();
        m_texInk[i].Reset();
        m_uavWet[i].Reset();
        m_srvWet[i].Reset();
        m_texWet[i].Reset();
    }
    m_uavPixel.Reset();
    m_texPixel.Reset();
    m_stagingPixel.Reset();
    m_stagingInk.Reset();
    m_stagingWet.Reset();
    m_context.Reset();
    m_device.Reset();
    m_width = 0;
    m_height = 0;
    m_currentPingPong = 0;
}

bool GpuSimulator::CreateDeviceAndShader()
{
    if (m_device && m_computeShader) return true;

    UINT createDeviceFlags = 0;
#if defined(_DEBUG)
    // createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevelOut;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        _countof(featureLevels),
        D3D11_SDK_VERSION,
        &m_device,
        &featureLevelOut,
        &m_context
    );

    if (FAILED(hr) || !m_device) return false;

    // コンピュートシェーダーのコンパイル
    ComPtr<ID3DBlob> csBlob;
    ComPtr<ID3DBlob> errorBlob;

    const char* targetProfile = (featureLevelOut >= D3D_FEATURE_LEVEL_11_0) ? "cs_5_0" : "cs_4_0";

    hr = D3DCompile(
        kPropagationCS,
        strlen(kPropagationCS),
        "InkPropagationCS",
        nullptr,
        nullptr,
        "CSMain",
        targetProfile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &csBlob,
        &errorBlob
    );

    if (FAILED(hr) || !csBlob) return false;

    hr = m_device->CreateComputeShader(
        csBlob->GetBufferPointer(),
        csBlob->GetBufferSize(),
        nullptr,
        &m_computeShader
    );

    if (FAILED(hr)) return false;

    // 定数バッファの生成
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(SimConstantBuffer);
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);

    return SUCCEEDED(hr);
}

bool GpuSimulator::CreateTextures(int width, int height)
{
    if (!m_device || width <= 0 || height <= 0) return false;

    m_width = width;
    m_height = height;
    m_currentPingPong = 0;

    // 1. 墨量テクスチャ (R32_SINT, 2面 Ping-Pong)
    for (int i = 0; i < 2; ++i)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32_SINT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_texInk[i]);
        if (FAILED(hr)) return false;

        hr = m_device->CreateUnorderedAccessView(m_texInk[i].Get(), nullptr, &m_uavInk[i]);
        if (FAILED(hr)) return false;

        hr = m_device->CreateShaderResourceView(m_texInk[i].Get(), nullptr, &m_srvInk[i]);
        if (FAILED(hr)) return false;
    }

    // 2. 水分テクスチャ (R8_UINT, 2面 Ping-Pong)
    for (int i = 0; i < 2; ++i)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8_UINT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_texWet[i]);
        if (FAILED(hr)) return false;

        hr = m_device->CreateUnorderedAccessView(m_texWet[i].Get(), nullptr, &m_uavWet[i]);
        if (FAILED(hr)) return false;

        hr = m_device->CreateShaderResourceView(m_texWet[i].Get(), nullptr, &m_srvWet[i]);
        if (FAILED(hr)) return false;
    }

    // 3. ピクセル出力テクスチャ (R8G8B8A8_UNORM)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, &m_texPixel);
        if (FAILED(hr)) return false;

        hr = m_device->CreateUnorderedAccessView(m_texPixel.Get(), nullptr, &m_uavPixel);
        if (FAILED(hr)) return false;
    }

    // 4. ステージングテクスチャ (CPU read/write 用)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        m_device->CreateTexture2D(&desc, nullptr, &m_stagingPixel);

        desc.Format = DXGI_FORMAT_R32_SINT;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        m_device->CreateTexture2D(&desc, nullptr, &m_stagingInk);

        desc.Format = DXGI_FORMAT_R8_UINT;
        m_device->CreateTexture2D(&desc, nullptr, &m_stagingWet);
    }

    return true;
}

bool GpuSimulator::Initialize(int width, int height)
{
    Release();
    if (!CreateDeviceAndShader()) return false;
    if (!CreateTextures(width, height)) return false;

    m_available = true;
    return true;
}

void GpuSimulator::Resize(int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (width == m_width && height == m_height && m_available) return;

    if (!m_device || !m_computeShader)
    {
        Initialize(width, height);
    }
    else
    {
        CreateTextures(width, height);
        m_available = true;
    }
}

bool GpuSimulator::UploadFromCpu(const int* inkData, const uint8_t* wetData, int width, int height)
{
    if (!m_available || !m_context || width != m_width || height != m_height) return false;
    if (!inkData || !wetData) return false;

    int srcIdx = m_currentPingPong;
    m_context->UpdateSubresource(m_texInk[srcIdx].Get(), 0, nullptr, inkData, width * sizeof(int), 0);
    m_context->UpdateSubresource(m_texWet[srcIdx].Get(), 0, nullptr, wetData, width * sizeof(uint8_t), 0);

    return true;
}

bool GpuSimulator::StepSimulation()
{
    if (!m_available || !m_context || !m_computeShader) return false;

    int readIdx = m_currentPingPong;
    int writeIdx = 1 - m_currentPingPong;

    // 定数バッファの更新
    SimConstantBuffer cb = {};
    cb.width = static_cast<uint32_t>(m_width);
    cb.height = static_cast<uint32_t>(m_height);
    cb.threshold = 350;
    cb.amount = 40;
    cb.wetLoss = 40;
    cb.wetDryRate = 1;
    cb.wetThreshold = 8;
    cb.maxInk = 450;
    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

    // パイプライン設定
    m_context->CSSetShader(m_computeShader.Get(), nullptr, 0);
    ID3D11Buffer* cbArray[1] = { m_constantBuffer.Get() };
    m_context->CSSetConstantBuffers(0, 1, cbArray);

    ID3D11ShaderResourceView* srvArray[2] = { m_srvInk[readIdx].Get(), m_srvWet[readIdx].Get() };
    m_context->CSSetShaderResources(0, 2, srvArray);

    ID3D11UnorderedAccessView* uavArray[3] = { m_uavInk[writeIdx].Get(), m_uavWet[writeIdx].Get(), m_uavPixel.Get() };
    m_context->CSSetUnorderedAccessViews(0, 3, uavArray, nullptr);

    // ディスパッチ (16x16 スレッドグループ)
    UINT dispatchX = (m_width + 15) / 16;
    UINT dispatchY = (m_height + 15) / 16;
    m_context->Dispatch(dispatchX, dispatchY, 1);

    // バインド解除
    ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
    m_context->CSSetShaderResources(0, 2, nullSRVs);

    ID3D11UnorderedAccessView* nullUAVs[3] = { nullptr, nullptr, nullptr };
    m_context->CSSetUnorderedAccessViews(0, 3, nullUAVs, nullptr);

    m_currentPingPong = writeIdx;
    return true;
}

bool GpuSimulator::DownloadToPixels(uint32_t* dstPixels, int width, int height)
{
    if (!m_available || !m_context || !m_stagingPixel || width != m_width || height != m_height || !dstPixels) return false;

    m_context->CopyResource(m_stagingPixel.Get(), m_texPixel.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_context->Map(m_stagingPixel.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    const uint8_t* srcRow = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < height; ++y)
    {
        std::memcpy(dstPixels + y * width, srcRow + y * mapped.RowPitch, width * sizeof(uint32_t));
    }

    m_context->Unmap(m_stagingPixel.Get(), 0);
    return true;
}

bool GpuSimulator::DownloadToPixelsRegion(uint32_t* dstPixels, int width, int height, int minX, int minY, int maxX, int maxY)
{
    if (!m_available || !m_context || !m_stagingPixel || width != m_width || height != m_height || !dstPixels) return false;

    minX = std::max(0, std::min(width - 1, minX));
    maxX = std::max(0, std::min(width - 1, maxX));
    minY = std::max(0, std::min(height - 1, minY));
    maxY = std::max(0, std::min(height - 1, maxY));
    if (minX > maxX || minY > maxY) return false;

    D3D11_BOX box;
    box.left = static_cast<UINT>(minX);
    box.right = static_cast<UINT>(maxX + 1);
    box.top = static_cast<UINT>(minY);
    box.bottom = static_cast<UINT>(maxY + 1);
    box.front = 0;
    box.back = 1;

    m_context->CopySubresourceRegion(m_stagingPixel.Get(), 0, minX, minY, 0, m_texPixel.Get(), 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = m_context->Map(m_stagingPixel.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    const uint8_t* srcRow = static_cast<const uint8_t*>(mapped.pData);
    size_t copyBytes = static_cast<size_t>(maxX - minX + 1) * sizeof(uint32_t);
    for (int y = minY; y <= maxY; ++y)
    {
        std::memcpy(dstPixels + (static_cast<size_t>(y) * width + minX),
                    srcRow + (static_cast<size_t>(y) * mapped.RowPitch + minX * sizeof(uint32_t)),
                    copyBytes);
    }

    m_context->Unmap(m_stagingPixel.Get(), 0);
    return true;
}

bool GpuSimulator::DownloadInkAndWet(int* dstInk, uint8_t* dstWet, int width, int height)
{
    if (!m_available || !m_context || width != m_width || height != m_height) return false;

    int curIdx = m_currentPingPong;

    if (dstInk && m_stagingInk)
    {
        m_context->CopyResource(m_stagingInk.Get(), m_texInk[curIdx].Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_stagingInk.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            const uint8_t* srcRow = static_cast<const uint8_t*>(mapped.pData);
            for (int y = 0; y < height; ++y)
            {
                std::memcpy(dstInk + y * width, srcRow + y * mapped.RowPitch, width * sizeof(int));
            }
            m_context->Unmap(m_stagingInk.Get(), 0);
        }
    }

    if (dstWet && m_stagingWet)
    {
        m_context->CopyResource(m_stagingWet.Get(), m_texWet[curIdx].Get());
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_stagingWet.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            const uint8_t* srcRow = static_cast<const uint8_t*>(mapped.pData);
            for (int y = 0; y < height; ++y)
            {
                std::memcpy(dstWet + y * width, srcRow + y * mapped.RowPitch, width * sizeof(uint8_t));
            }
            m_context->Unmap(m_stagingWet.Get(), 0);
        }
    }

    return true;
}

bool GpuSimulator::DownloadInkAndWetRegion(int* dstInk, uint8_t* dstWet, int width, int height, int minX, int minY, int maxX, int maxY)
{
    if (!m_available || !m_context || width != m_width || height != m_height) return false;

    minX = std::max(0, std::min(width - 1, minX));
    maxX = std::max(0, std::min(width - 1, maxX));
    minY = std::max(0, std::min(height - 1, minY));
    maxY = std::max(0, std::min(height - 1, maxY));
    if (minX > maxX || minY > maxY) return false;

    int curIdx = m_currentPingPong;
    D3D11_BOX box;
    box.left = static_cast<UINT>(minX);
    box.right = static_cast<UINT>(maxX + 1);
    box.top = static_cast<UINT>(minY);
    box.bottom = static_cast<UINT>(maxY + 1);
    box.front = 0;
    box.back = 1;

    if (dstInk && m_stagingInk)
    {
        m_context->CopySubresourceRegion(m_stagingInk.Get(), 0, minX, minY, 0, m_texInk[curIdx].Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_stagingInk.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            const uint8_t* srcRow = static_cast<const uint8_t*>(mapped.pData);
            size_t copyBytes = static_cast<size_t>(maxX - minX + 1) * sizeof(int);
            for (int y = minY; y <= maxY; ++y)
            {
                std::memcpy(dstInk + (static_cast<size_t>(y) * width + minX),
                            srcRow + (static_cast<size_t>(y) * mapped.RowPitch + minX * sizeof(int)),
                            copyBytes);
            }
            m_context->Unmap(m_stagingInk.Get(), 0);
        }
    }

    if (dstWet && m_stagingWet)
    {
        m_context->CopySubresourceRegion(m_stagingWet.Get(), 0, minX, minY, 0, m_texWet[curIdx].Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_context->Map(m_stagingWet.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            const uint8_t* srcRow = static_cast<const uint8_t*>(mapped.pData);
            size_t copyBytes = static_cast<size_t>(maxX - minX + 1) * sizeof(uint8_t);
            for (int y = minY; y <= maxY; ++y)
            {
                std::memcpy(dstWet + (static_cast<size_t>(y) * width + minX),
                            srcRow + (static_cast<size_t>(y) * mapped.RowPitch + minX * sizeof(uint8_t)),
                            copyBytes);
            }
            m_context->Unmap(m_stagingWet.Get(), 0);
        }
    }

    return true;
}
