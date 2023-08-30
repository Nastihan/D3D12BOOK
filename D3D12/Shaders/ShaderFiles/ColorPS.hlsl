
struct PS_Iput
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
};
	
float4 main(PS_Iput input) : SV_TARGET
{
    return input.Color;
}