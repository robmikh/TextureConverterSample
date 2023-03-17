#include "pch.h"
#include "TextureConverter.h"

namespace shaders
{
    namespace pixelConversion
    {
        #include "PixelConversionShader.h"
    }
}

namespace winrt
{
	using namespace Windows::Foundation;
    using namespace Windows::Graphics;
}

namespace util
{
    using namespace robmikh::common::uwp;
    using namespace robmikh::common::desktop;
}

__declspec(align(16)) struct TextureInfo
{
    uint32_t Width;
    uint32_t Height;
};

winrt::com_ptr<ID2D1Bitmap1> CreateBitmapFromTexture(
    winrt::com_ptr<ID3D11Texture2D> const& texture,
    winrt::com_ptr<ID2D1DeviceContext> const& d2dContext);

TextureConverter::TextureConverter(
    winrt::com_ptr<ID3D11Device> const& d3dDevice,
    winrt::com_ptr<ID2D1Device> const& d2dDevice,
    uint32_t width, 
    uint32_t height)
{
    m_d3dDevice = d3dDevice;
    d3dDevice->GetImmediateContext(m_d3dContext.put());
    m_targetWidth = width;
    m_targetHeight = height;

    // Create our device context
    m_d2dDevice = d2dDevice;
    winrt::check_hresult(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_d2dContext.put()));

    // Create our textures
    {
        // Create our intermediate texture
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = m_targetWidth;
        desc.Height = m_targetHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_intermediateTexture.put()));

        // Create our output texture
        desc.Width = desc.Width * 3;
        desc.Format = DXGI_FORMAT_R8_UINT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_outputTexture.put()));

        // Create our staging texture for our output
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, m_stagingTexture.put()));
    }

    // Create our views
    {
        winrt::check_hresult(d3dDevice->CreateShaderResourceView(m_intermediateTexture.get(), nullptr, m_intermediateShaderResourceView.put()));
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.Format = DXGI_FORMAT_R8_UINT;
        desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        winrt::check_hresult(d3dDevice->CreateUnorderedAccessView(m_outputTexture.get(), &desc, m_outputUnorderedAccessView.put()));
    }

    // Create a D2D bitmap for our intermediate texture
    m_intermediateBitmap = CreateBitmapFromTexture(m_intermediateTexture, m_d2dContext);
    m_d2dContext->SetTarget(m_intermediateBitmap.get());

    // Load our compute shader
    winrt::check_hresult(d3dDevice->CreateComputeShader(
        shaders::pixelConversion::g_main,
        ARRAYSIZE(shaders::pixelConversion::g_main),
        nullptr,
        m_conversionShader.put()));

    // Create the texture info buffer
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(TextureInfo);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        TextureInfo initialInfo = {};
        initialInfo.Width = m_targetWidth;
        initialInfo.Height = m_targetHeight;
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = reinterpret_cast<void*>(&initialInfo);
        winrt::check_hresult(d3dDevice->CreateBuffer(&desc, &initData, m_textureInfoBuffer.put()));
    }
}

void TextureConverter::ProcessInput(winrt::com_ptr<ID3D11Texture2D> const& texture, std::vector<byte>& bytes)
{
    // First we scale the image to our desired size
    {
        auto inputBitmap = CreateBitmapFromTexture(texture, m_d2dContext);

        m_d2dContext->BeginDraw();
        auto endDraw = wil::scope_exit([d2dContext = m_d2dContext]
            {
                winrt::check_hresult(d2dContext->EndDraw());
            });

        auto destRect = D2D1_RECT_F{ 0, 0, static_cast<float>(m_targetWidth), static_cast<float>(m_targetHeight) };

        m_d2dContext->Clear(D2D1_COLOR_F{ 0, 0, 0, 1 });
        m_d2dContext->DrawBitmap(
            inputBitmap.get(),
            &destRect,
            1.0f,
            D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
            nullptr);
    }

    // Next, run the compute shader
    m_d3dContext->CSSetShader(m_conversionShader.get(), nullptr, 0);
    std::vector<ID3D11ShaderResourceView*> srvs = { m_intermediateShaderResourceView.get() };
    m_d3dContext->CSSetShaderResources(0, static_cast<uint32_t>(srvs.size()), srvs.data());
    std::vector<ID3D11Buffer*> constants = { m_textureInfoBuffer.get() };
    m_d3dContext->CSSetConstantBuffers(0, static_cast<uint32_t>(constants.size()), constants.data());
    std::vector<ID3D11UnorderedAccessView*> uavs = { m_outputUnorderedAccessView.get() };
    m_d3dContext->CSSetUnorderedAccessViews(0, static_cast<uint32_t>(uavs.size()), uavs.data(), nullptr);

    m_d3dContext->Dispatch((m_targetWidth / 8) + 1, (m_targetHeight / 8) + 1, 1);

    // Copy the result to our staging buffer so we can
    // copy it into system memory.
    m_d3dContext->CopyResource(m_stagingTexture.get(), m_outputTexture.get());

    // Copy the bytes
    auto bytesStride = m_targetWidth * 3;
    auto bytesSize = bytesStride * m_targetHeight;
    if (bytes.size() < bytesSize)
    {
        bytes.resize(bytesSize);
    }

    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        winrt::check_hresult(m_d3dContext->Map(m_stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped));

        auto source = reinterpret_cast<byte*>(mapped.pData);
        auto dest = bytes.data();
        for (uint32_t i = 0; i < m_targetHeight; i++)
        {
            memcpy(dest, source, bytesStride);

            source += mapped.RowPitch;
            dest += bytesStride;
        }
        m_d3dContext->Unmap(m_stagingTexture.get(), 0);
    }
}

winrt::com_ptr<ID2D1Bitmap1> CreateBitmapFromTexture(
    winrt::com_ptr<ID3D11Texture2D> const& texture,
    winrt::com_ptr<ID2D1DeviceContext> const& d2dContext)
{
    auto dxgiSurface = texture.as<IDXGISurface>();
    winrt::com_ptr<ID2D1Bitmap1> bitmap;
    winrt::check_hresult(d2dContext->CreateBitmapFromDxgiSurface(dxgiSurface.get(), nullptr, bitmap.put()));
    return bitmap;
}