RWTexture2D<uint> cov : register(u0);
float4 vs(float2 pos : POSITION) : SV_Position { return float4(pos, 0.5, 1.0); }
void ps(float4 pos : SV_Position, uint c : SV_Coverage) { InterlockedAdd(cov[uint2(pos.xy)], countbits(c)); }
