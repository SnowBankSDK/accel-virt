struct Varyings {
    float4 position : SV_Position;
    float4 colour : COLOR;
    noperspective float2 flat_plane : TEXCOORD0;
    nointerpolation float constant : TEXCOORD1;
    centroid float2 inside : TEXCOORD2;
};
struct PixelVaryings {
    float4 position : SV_Position;
    float4 colour : COLOR;
    noperspective float2 flat_plane : TEXCOORD0;
    nointerpolation float constant : TEXCOORD1;
    centroid float2 inside : TEXCOORD2;
};
Varyings vs(float2 pos : POSITION)
{
    Varyings v;
    v.position = float4(pos, 0.5, 1.0);
    v.colour = float4(pos, 0.0, 1.0);
    v.flat_plane = pos;
    v.constant = pos.x;
    v.inside = pos;
    return v;
}
float4 ps_per_sample(Varyings v, uint index : SV_SampleIndex) : SV_Target
{
    return v.colour * float(index + 1u) + float4(v.flat_plane, v.constant, 0.0) + float4(v.inside, 0.0, 0.0);
}
float4 ps_per_pixel(PixelVaryings v) : SV_Target
{
    return v.colour + float4(v.flat_plane, v.constant, 0.0) + float4(v.inside, 0.0, 0.0);
}
