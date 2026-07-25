ByteAddressBuffer Triangles : register(t0);
RWStructuredBuffer<uint> TemporaryField : register(u0);
RWStructuredBuffer<uint> FinalField : register(u1);

cbuffer RasterConstants : register(b0)
{
    uint3 GridSize;
    uint ItemCount;
    float3 GridOrigin;
    float VoxelSize;
    uint DilationRadius;
    uint DispatchGroupsX;
};

static const uint ThreadGroupSize = 64;
static const uint TriangleStride = 100;

uint LinearThreadIndex(uint groupIndex, uint3 groupId)
{
    uint linearGroup = groupId.x + groupId.y * DispatchGroupsX;
    uint requiredGroups = ((ItemCount - 1) / ThreadGroupSize) + 1;
    if (linearGroup >= requiredGroups)
    {
        return 0xffffffffu;
    }
    return linearGroup * ThreadGroupSize + groupIndex;
}

uint CellIndex(int3 coordinates)
{
    return (uint(coordinates.z) * GridSize.y + uint(coordinates.y)) *
        GridSize.x + uint(coordinates.x);
}

float3 LoadPosition(uint address)
{
    return asfloat(Triangles.Load3(address));
}

float4x4 LoadLocalToWorld(uint address)
{
    float4 row0 = asfloat(Triangles.Load4(address));
    float4 row1 = asfloat(Triangles.Load4(address + 16));
    float4 row2 = asfloat(Triangles.Load4(address + 32));
    float4 row3 = asfloat(Triangles.Load4(address + 48));
    return float4x4(row0, row1, row2, row3);
}

[numthreads(ThreadGroupSize, 1, 1)]
void MarkTriangles(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    uint triangleIndex = LinearThreadIndex(groupIndex, groupId);
    if (triangleIndex >= ItemCount)
    {
        return;
    }

    uint address = triangleIndex * TriangleStride;
    float3 a = LoadPosition(address);
    float3 b = LoadPosition(address + 12);
    float3 c = LoadPosition(address + 24);
    float4x4 localToWorld = LoadLocalToWorld(address + 36);
    a = mul(float4(a, 1.0f), localToWorld).xyz;
    b = mul(float4(b, 1.0f), localToWorld).xyz;
    c = mul(float4(c, 1.0f), localToWorld).xyz;

    float3 aabbMin = min(a, min(b, c));
    float3 aabbMax = max(a, max(b, c));
    int3 gridMin = int3((aabbMin - GridOrigin) / VoxelSize);
    int3 gridMax = int3((aabbMax - GridOrigin) / VoxelSize);
    int3 maximumCoordinate = int3(GridSize) - 1;
    gridMin = clamp(gridMin, int3(0, 0, 0), maximumCoordinate);
    gridMax = clamp(gridMax, int3(0, 0, 0), maximumCoordinate);

    for (int z = gridMin.z; z <= gridMax.z; ++z)
    {
        for (int y = gridMin.y; y <= gridMax.y; ++y)
        {
            for (int x = gridMin.x; x <= gridMax.x; ++x)
            {
                uint previousValue;
                InterlockedExchange(
                    TemporaryField[CellIndex(int3(x, y, z))],
                    1,
                    previousValue);
            }
        }
    }
}

[numthreads(ThreadGroupSize, 1, 1)]
void DilateVoxels(uint groupIndex : SV_GroupIndex, uint3 groupId : SV_GroupID)
{
    uint cellIndex = LinearThreadIndex(groupIndex, groupId);
    if (cellIndex >= ItemCount)
    {
        return;
    }

    uint cellsPerPlane = GridSize.x * GridSize.y;
    uint currentZ = cellIndex / cellsPerPlane;
    uint planeIndex = cellIndex % cellsPerPlane;
    uint currentY = planeIndex / GridSize.x;
    uint currentX = planeIndex % GridSize.x;
    uint3 currentUnsigned = uint3(currentX, currentY, currentZ);
    uint3 maximumCoordinate = GridSize - 1;
    uint3 negativeExtent = min(currentUnsigned, DilationRadius);
    uint3 positiveExtent =
        min(maximumCoordinate - currentUnsigned, DilationRadius);
    int3 gridMin = int3(currentUnsigned - negativeExtent);
    int3 gridMax = int3(currentUnsigned + positiveExtent);
    int3 current = int3(currentUnsigned);
    uint64_t radiusSquared =
        uint64_t(DilationRadius) * uint64_t(DilationRadius);

    FinalField[cellIndex] = 0;
    for (int z = gridMin.z; z <= gridMax.z; ++z)
    {
        for (int y = gridMin.y; y <= gridMax.y; ++y)
        {
            for (int x = gridMin.x; x <= gridMax.x; ++x)
            {
                int3 distance = int3(x, y, z) - current;
                uint64_t dx = uint64_t(abs(distance.x));
                uint64_t dy = uint64_t(abs(distance.y));
                uint64_t dz = uint64_t(abs(distance.z));
                uint64_t distanceSquared =
                    dx * dx + dy * dy + dz * dz;
                if (distanceSquared > radiusSquared)
                {
                    continue;
                }

                if (TemporaryField[CellIndex(int3(x, y, z))] == 1)
                {
                    FinalField[cellIndex] = 1;
                    return;
                }
            }
        }
    }
}
