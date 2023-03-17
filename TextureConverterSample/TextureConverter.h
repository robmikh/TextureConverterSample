#pragma once

class TextureConverter 
{
public:
	TextureConverter(
		winrt::com_ptr<ID3D11Device> const& d3dDevice,
		winrt::com_ptr<ID2D1Device> const& d2dDevice,
		uint32_t width, 
		uint32_t height);

	void ProcessInput(winrt::com_ptr<ID3D11Texture2D> const& texture, std::vector<byte>& bytes);

private:
	winrt::com_ptr<ID3D11Device> m_d3dDevice;
	uint32_t m_targetWidth = 0;
	uint32_t m_targetHeight = 0;
	winrt::com_ptr<ID2D1Device> m_d2dDevice;
	winrt::com_ptr<ID2D1DeviceContext> m_d2dContext;
	winrt::com_ptr<ID3D11DeviceContext> m_d3dContext;
	winrt::com_ptr<ID3D11Texture2D> m_intermediateTexture;
	winrt::com_ptr<ID3D11Texture2D> m_outputTexture;
	winrt::com_ptr<ID3D11ShaderResourceView> m_intermediateShaderResourceView;
	winrt::com_ptr<ID3D11UnorderedAccessView> m_outputUnorderedAccessView;
	winrt::com_ptr<ID3D11Texture2D> m_stagingTexture;
	winrt::com_ptr<ID2D1Bitmap1> m_intermediateBitmap;
	winrt::com_ptr<ID3D11ComputeShader> m_conversionShader;
	winrt::com_ptr<ID3D11Buffer> m_textureInfoBuffer;
};