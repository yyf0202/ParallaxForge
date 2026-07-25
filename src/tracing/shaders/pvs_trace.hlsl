struct TracePayload
{
    uint local_probe;
};

RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<float3> Probes : register(t1);
RWBuffer<uint> VisibilityWords : register(u0);

cbuffer TraceConstants : register(b0)
{
    float4 FaceOrigins[6];
    float4 FaceExtentsU[6];
    float4 FaceExtentsV[6];
    uint FaceResolution;
    uint WordsPerObject;
    float MaxDistance;
    uint Padding;
};

[shader("raygeneration")]
void RayGeneration()
{
    uint3 dispatch_index = DispatchRaysIndex();
    uint pixel_x = dispatch_index.x % FaceResolution;
    uint pixel_y = dispatch_index.x / FaceResolution;
    float u = (float(pixel_x) + 0.5f) / float(FaceResolution);
    float v = (float(pixel_y) + 0.5f) / float(FaceResolution);
    uint face = dispatch_index.y;
    float3 direction = normalize(
        FaceOrigins[face].xyz +
        u * FaceExtentsU[face].xyz +
        v * FaceExtentsV[face].xyz);

    RayDesc ray;
    ray.Origin = Probes[dispatch_index.z];
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = MaxDistance;

    TracePayload payload;
    payload.local_probe = dispatch_index.z;
    TraceRay(
        Scene,
        RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
        0xff,
        0,
        0,
        0,
        ray,
        payload);
}

[shader("miss")]
void Miss(inout TracePayload payload)
{
}

[shader("closesthit")]
void ClosestHit(
    inout TracePayload payload,
    BuiltInTriangleIntersectionAttributes attributes)
{
    uint object_slot = InstanceID();
    uint word_index =
        object_slot * WordsPerObject + payload.local_probe / 32u;
    uint previous_value;
    InterlockedOr(
        VisibilityWords[word_index],
        1u << (payload.local_probe % 32u),
        previous_value);
}
