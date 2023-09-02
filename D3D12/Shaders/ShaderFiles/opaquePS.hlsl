
struct PS_Input
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
};


float4 main(PS_Input pin) : SV_Target
{
    return pin.Color;
}