cbuffer TextureInfo : register(b0)
{
    uint Width;
    uint Height;
};

Texture2D<unorm float4> inputTexture : register(t0);

RWTexture2D<uint> outputTexture : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 inputPosition = DTid.xy;

    if (inputPosition.x < Width && inputPosition.y < Height)
    {
        float4 pixel = inputTexture[inputPosition];
        float b = pixel.z;
        float g = pixel.y;
        float r = pixel.x;

        uint2 baseOutputPosition =
        {
            inputPosition.x * 3,
            inputPosition.y
        };

        uint2 rChannelOffset = { 0, 0 };
        uint2 gChannelOffset = { 1, 0 };
        uint2 bChannelOffset = { 2, 0 };

        outputTexture[baseOutputPosition + rChannelOffset] = (uint)(r * 255.0f);
        outputTexture[baseOutputPosition + gChannelOffset] = (uint)(g * 255.0f);
        outputTexture[baseOutputPosition + bChannelOffset] = (uint)(b * 255.0f);
    }
}