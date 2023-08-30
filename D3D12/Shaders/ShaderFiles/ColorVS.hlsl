cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
};

struct VS_Input
{
    float3 PosL : POSITION;
    float4 Color : COLOR;
};

struct VS_Output
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
};

VS_Output main(VS_Input input)
{
    VS_Output vout;
	
	// Transform to homogeneous clip space.
    vout.PosH = mul(float4(input.PosL, 1.0f), gWorldViewProj);
	
	// Just pass vertex color into the pixel shader.
    vout.Color = input.Color;
    
    return vout;
}