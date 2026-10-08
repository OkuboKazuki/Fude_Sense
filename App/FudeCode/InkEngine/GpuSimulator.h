#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

using Microsoft::WRL::ComPtr;

// D3D11 Compute Shader 定数バッファ
struct alignas(16) SimConstantBuffer
{
    uint32_t width;
    uint32_t height;
    int32_t  threshold;     // 350
    int32_t  amount;        // 40
    int32_t  wetLoss;       // 40
    int32_t  wetDryRate;    // 1
    int32_t  wetThreshold;  // 8
    int32_t  maxInk;        // 450
};

// Direct3D 11 コンピュートシェーダーによる GPU 墨汁浸透シミュレータ
class GpuSimulator
{
public:
    GpuSimulator();
    ~GpuSimulator();

    // D3D11 デバイスおよびシェーダーの初期化
    bool Initialize(int width, int height);
    void Release();
    void Resize(int width, int height);

    // GPU コンピュートシェーダーで 1 ステップ浸透を進める
    bool StepSimulation();

    // CPU 側の墨・水分バッファを GPU テクスチャへ転送（ストローク描画後）
    bool UploadFromCpu(const int* inkData, const uint8_t* wetData, int width, int height);

    // GPU テクスチャから CPU ピクセルバッファ (ARGB) を読み戻す（描画用）
    bool DownloadToPixels(uint32_t* dstPixels, int width, int height);
    bool DownloadToPixelsRegion(uint32_t* dstPixels, int width, int height, int minX, int minY, int maxX, int maxY);

    // GPU テクスチャから墨量・水分バッファを読み戻す（一画戻す/スナップショット用）
    bool DownloadInkAndWet(int* dstInk, uint8_t* dstWet, int width, int height);
    bool DownloadInkAndWetRegion(int* dstInk, uint8_t* dstWet, int width, int height, int minX, int minY, int maxX, int maxY);

    // スタンプ打刻ピクセルを GPU テクスチャへ直接部分転送（にじみシミュレーションを挟まず鮮明に反映）
    bool UploadPixels(const uint32_t* pixels, int width, int height);
    bool UploadPixelsRegion(const uint32_t* pixels, int width, int height, int minX, int minY, int maxX, int maxY);
    void ClearTextures();

    // Direct2D 1.1 DirectX 共有連携用アクセサ
    ComPtr<ID3D11Device>        GetDevice() const { return m_device; }
    ComPtr<ID3D11DeviceContext> GetContext() const { return m_context; }
    ComPtr<ID3D11Texture2D>     GetPixelTexture() const { return m_texPixel; }

    bool IsAvailable() const { return m_available; }
    // ドライバの更新・GPU のリセット・スリープ復帰などでデバイスが失われたか。
    // 失われたデバイスへの呼び出しは黙って何もしないので、呼び出し側で見て作り直す。
    bool IsDeviceLost() const { return m_device && FAILED(m_device->GetDeviceRemovedReason()); }

private:
    bool CreateDeviceAndShader();
    bool CreateTextures(int width, int height);

private:
    bool m_available = false;
    int m_width = 0;
    int m_height = 0;
    int m_currentPingPong = 0;

    ComPtr<ID3D11Device>        m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11ComputeShader> m_computeShader;
    ComPtr<ID3D11Buffer>        m_constantBuffer;

    // Ping-Pong テクスチャ（墨量: R32_SINT, 水分: R8_UINT）
    ComPtr<ID3D11Texture2D>          m_texInk[2];
    ComPtr<ID3D11UnorderedAccessView> m_uavInk[2];
    ComPtr<ID3D11ShaderResourceView> m_srvInk[2];

    ComPtr<ID3D11Texture2D>          m_texWet[2];
    ComPtr<ID3D11UnorderedAccessView> m_uavWet[2];
    ComPtr<ID3D11ShaderResourceView> m_srvWet[2];

    // ピクセル出力テクスチャ (R8G8B8A8_UNORM)
    ComPtr<ID3D11Texture2D>          m_texPixel;
    ComPtr<ID3D11UnorderedAccessView> m_uavPixel;

    // CPU ステージングテクスチャ（アップロード／ダウンロード用）
    ComPtr<ID3D11Texture2D>          m_stagingPixel;
    ComPtr<ID3D11Texture2D>          m_stagingInk;
    ComPtr<ID3D11Texture2D>          m_stagingWet;
};
