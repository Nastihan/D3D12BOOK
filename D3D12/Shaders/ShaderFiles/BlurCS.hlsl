
RWTexture2D<float4> tex : register(u0);
static const int blurRadius = 5;

#define N 256
#define CacheSize (N + 2* blurRadius)
groupshared float4 gCache[CacheSize];

[numthreads(N, 1, 1)]
void main( uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID )
{
    uint width = 0;
    uint height = 0;
    tex.GetDimensions(width, height);
    
    if (groupThreadID.x < blurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int x = max(dispatchThreadID.x - blurRadius, 0);
        gCache[groupThreadID.x] = tex[int2(x, dispatchThreadID.y)];
    }
    if (groupThreadID.x >= N - blurRadius)
    {
		// Clamp out of bound samples that occur at image borders.
        int x = min(dispatchThreadID.x + blurRadius, width - 1);
        gCache[groupThreadID.x + 2 * blurRadius] = tex[int2(x, dispatchThreadID.y)];
    }
    
    GroupMemoryBarrierWithGroupSync();
    
    float4 blurColor = (0.0f, 0.0f, 0.0f, 0.0f);
    
    for (int i = -blurRadius; i <= blurRadius; i++)
    {
        blurColor += tex[dispatchThreadID.xy + i];
    }
    
    blurColor /= (blurRadius * blurRadius);
    
    tex[dispatchThreadID.xy] = blurColor;
    
        
}