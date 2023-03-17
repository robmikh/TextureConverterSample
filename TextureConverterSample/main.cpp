#include "pch.h"
#include "TextureConverter.h"

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Metadata;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::uwp;
    using namespace robmikh::common::desktop;
}

winrt::com_ptr<ID3D11Texture2D> TakeScreenshot(winrt::com_ptr<ID3D11Device> const& d3dDevice);
void DumpImageToDisk(std::vector<byte> const& bytes, std::string const& name, uint32_t width, uint32_t height);

int __stdcall wmain()
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    auto useDebugLayer = true;

    // Init D3D
    uint32_t d3dFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (useDebugLayer)
    {
        d3dFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
    auto d3dDevice = util::CreateD3DDevice(d3dFlags);

    // Init our converter
    uint32_t width = 640;
    uint32_t height = 480;
    auto converter = TextureConverter(d3dDevice, width, height);
    
    // Our input will be a screenshot of the primary monitor
    auto inputTexture = TakeScreenshot(d3dDevice);

    // Convert our texture
    std::vector<byte> bytes;
    converter.ProcessInput(inputTexture, bytes);

    // Dump raw bytes to disk
    DumpImageToDisk(bytes, "convertedBitmap", width, height);

    return 0;
}

winrt::com_ptr<ID3D11Texture2D> TakeScreenshot(winrt::com_ptr<ID3D11Device> const& d3dDevice)
{
    auto device = CreateDirect3DDevice(d3dDevice.as<IDXGIDevice>().get());

    // Get the primary monitor
    auto monitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    auto item = util::CreateCaptureItemForMonitor(monitor);
    auto itemSize = item.Size();

    // Setup the frame pool
    auto framePool = winrt::Direct3D11CaptureFramePool::CreateFreeThreaded(
        device,
        winrt::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        1,
        itemSize);
    auto session = framePool.CreateCaptureSession(item);

    wil::shared_event captureEvent(wil::EventOptions::None);
    winrt::Direct3D11CaptureFrame frame{ nullptr };
    framePool.FrameArrived([captureEvent, &frame](auto&& framePool, auto&&)
        {
            frame = framePool.TryGetNextFrame();
            captureEvent.SetEvent();
        });

    session.IsCursorCaptureEnabled(false);
    // This API was introduced in Windows 11
    if (winrt::ApiInformation::IsPropertyPresent(winrt::name_of<winrt::GraphicsCaptureSession>(), L"IsBorderRequired"))
    {
        session.IsBorderRequired(false);
    }

    // Wait for a frame to come back
    session.StartCapture();
    captureEvent.wait();

    // Stop the capture
    framePool.Close();
    session.Close();

    auto texture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
    return texture;
}

void DumpImageToDisk(std::vector<byte> const& bytes, std::string const& name, uint32_t width, uint32_t height)
{
    auto filePath = std::filesystem::current_path();
    {
        std::stringstream fileNameStream;
        fileNameStream << name.c_str() << "_" << width << "x" << height << ".bin";
        filePath /= fileNameStream.str();
    }
    std::ofstream file(filePath, std::ios::out | std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

