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
    using namespace robmikh::common::wcli;
}

struct Options
{
    bool DxDebug;
    uint32_t Width;
    uint32_t Height;
};

winrt::com_ptr<ID3D11Texture2D> TakeScreenshot(winrt::com_ptr<ID3D11Device> const& d3dDevice);
void DumpImageToDisk(std::vector<byte> const& bytes, std::string const& name, uint32_t width, uint32_t height);
std::optional<Options> ParseOptions(int argc, wchar_t* argv[], bool& error);

int __stdcall wmain(int argc, wchar_t* argv[])
{
    // Initialize COM
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // Parse args
    bool error = false;
    auto options = ParseOptions(argc, argv, error);
    if (error || !options.has_value())
    {
        return error ? 1 : 0;
    }
    auto useDebugLayer = options->DxDebug;
    uint32_t width = options->Width;
    uint32_t height = options->Height;

    // Init D3D
    uint32_t d3dFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (useDebugLayer)
    {
        d3dFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
    // This is a helper wrapper around D3D11CreateDevice. It will attempt to create a
    // hardware device, and will fallback to WARP. The source can be found here:
    // https://github.com/robmikh/robmikh.common/blob/aaa31a1bcd4fac19ce4c544d2cfd415612add7f0/robmikh.common/include/robmikh.common/d3dHelpers.h#L60-L79
    auto d3dDevice = util::CreateD3DDevice(d3dFlags);

    // Init D2D
    auto d2dDebugFlag = D2D1_DEBUG_LEVEL_NONE;
    if (useDebugLayer)
    {
        d2dDebugFlag = D2D1_DEBUG_LEVEL_INFORMATION;
    }
    // These are wrappers around D2D1CreateFactory and ID2D1Factory1::CreateDevice.
    // https://github.com/robmikh/robmikh.common/blob/aaa31a1bcd4fac19ce4c544d2cfd415612add7f0/robmikh.common/include/robmikh.common/d3dHelpers.h#L81-L89
    auto d2dFactory = util::CreateD2DFactory(d2dDebugFlag);
    // https://github.com/robmikh/robmikh.common/blob/aaa31a1bcd4fac19ce4c544d2cfd415612add7f0/robmikh.common/include/robmikh.common/d3dHelpers.h#L53-L58
    auto d2dDevice = util::CreateD2DDevice(d2dFactory, d3dDevice);

    // Init our converter
    auto converter = TextureConverter(d3dDevice, d2dDevice, width, height);
    
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
    // This is a wrapper around IGraphicsCaptureItemInterop::CreateForMonitor. For
    // more details, as well as how to capture a window, visit:
    // https://github.com/robmikh/Win32CaptureSample#hwnd-or-hmonitor-based-capture
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

std::optional<uint32_t> ParseNumberString(std::wstring const& numberString)
{
    try
    {
        uint32_t width = std::stoi(numberString);
        return std::optional(width);
    }
    catch (...)
    {
    }
    return std::nullopt;
}

std::optional<Options> ParseOptions(int argc, wchar_t* argv[], bool& error)
{
    error = true;
    // Much of this method uses helpers from the robmikh.common package.
    // I wouldn't recommend using this part, but if you're curious it can 
    // be found here: https://github.com/robmikh/robmikh.common/blob/master/robmikh.common/include/robmikh.common/wcliparse.h
    std::vector<std::wstring> args(argv + 1, argv + argc);
    if (util::impl::GetFlag(args, L"-help") || util::impl::GetFlag(args, L"/?"))
    {
        wprintf(L"TextureConverterSample.exe\n");
        wprintf(L"A sample that shows how to use D2D and D3D11 to convert between different texture dimensions\n");
        wprintf(L" and formats. A screenshot of the primary monitor (BGRA8) is converted to RGB8 and saved to disk.\n");
        wprintf(L"\n");
        wprintf(L"Options:\n");
        wprintf(L"  -width  [value] (optional) Specify the output width. Default is 640.\n");
        wprintf(L"  -height [value] (optional) Specify the output height. Default is 480.\n");
        wprintf(L"\n");
        wprintf(L"Flags:\n");
        wprintf(L"  -dxDebug        (optional) Use the D3D and D2D debug layers.\n");
        wprintf(L"\n");
        error = false;
        return std::nullopt;
    }
    auto widthString = util::impl::GetFlagValue(args, L"-width", L"-w");
    auto heightString = util::impl::GetFlagValue(args, L"-height", L"-h");
    bool dxDebug = util::impl::GetFlag(args, L"-dxDebug") || util::impl::GetFlag(args, L"/dxDebug");
    if (dxDebug)
    {
        wprintf(L"Using D3D and D2D debug layers...\n");
    }
    
    uint32_t width = 640;
    if (!widthString.empty())
    {
        if (auto parsedWidth = ParseNumberString(widthString))
        {
            width = parsedWidth.value();
        }
        else
        {
            wprintf(L"Invalid width specified!\n");
            return std::nullopt;
        }
    }
    uint32_t height = 480;
    if (!heightString.empty())
    {
        if (auto parsedHeight = ParseNumberString(heightString))
        {
            height = parsedHeight.value();
        }
        else
        {
            wprintf(L"Invalid width specified!\n");
            return std::nullopt;
        }
    }
    wprintf(L"Using a target width and height of %u x %u...\n", width, height);
    
    error = false;
    return std::optional(Options { dxDebug, width, height });
}