StructuredBuffer<uint> FinalField : register(t0);
ByteAddressBuffer AlwaysIncludeVolumes : register(t1);
AppendStructuredBuffer<float3> Probes : register(u0);

cbuffer ProbeConstants : register(b0)
{
    uint3 BlockCount;
    uint TotalBlockCount;
    float3 BoundsMin;
    float Delta;
    uint3 GridSize;
    float VoxelSize;
    float3 GridOrigin;
    uint VolumeCount;
};

static const float StorageBlockSize = 4.0f;
static const uint CandidateCount = 9;
static const uint VolumeStride = 64;

uint CellIndex(uint3 coordinates)
{
    return (coordinates.z * GridSize.y + coordinates.y) *
        GridSize.x + coordinates.x;
}

bool QueryVoxel(float3 position)
{
    float3 gridMaximum =
        GridOrigin + float3(GridSize) * VoxelSize;
    if (any(position < GridOrigin) || any(position >= gridMaximum))
    {
        return false;
    }

    uint3 coordinates = uint3((position - GridOrigin) / VoxelSize);
    return FinalField[CellIndex(coordinates)] != 0;
}

float4x4 LoadWorldToLocal(uint volumeIndex)
{
    uint address = volumeIndex * VolumeStride;
    float4 row0 = asfloat(AlwaysIncludeVolumes.Load4(address));
    float4 row1 = asfloat(AlwaysIncludeVolumes.Load4(address + 16));
    float4 row2 = asfloat(AlwaysIncludeVolumes.Load4(address + 32));
    float4 row3 = asfloat(AlwaysIncludeVolumes.Load4(address + 48));
    return float4x4(row0, row1, row2, row3);
}

bool InsideAlwaysIncludeVolume(float3 position)
{
    for (uint volumeIndex = 0; volumeIndex < VolumeCount; ++volumeIndex)
    {
        float3 localPoint =
            mul(float4(position, 1.0f), LoadWorldToLocal(volumeIndex)).xyz;
        if (all(localPoint >= -1.0f) && all(localPoint <= 1.0f))
        {
            return true;
        }
    }
    return false;
}

float3 CandidatePoint(float3 blockMinimum, uint candidateIndex)
{
    if (candidateIndex == 8)
    {
        return blockMinimum + 2.0f;
    }

    bool3 maximumCorner = bool3(
        (candidateIndex & 1) != 0,
        (candidateIndex & 2) != 0,
        (candidateIndex & 4) != 0);
    float maximumOffset = StorageBlockSize - Delta;
    float3 offset = float3(
        maximumCorner.x ? maximumOffset : Delta,
        maximumCorner.y ? maximumOffset : Delta,
        maximumCorner.z ? maximumOffset : Delta);
    return blockMinimum + offset;
}

[numthreads(1, 1, 1)]
void GenerateProbes()
{
    for (uint blockIndex = 0;
         blockIndex < TotalBlockCount;
         ++blockIndex)
    {
        uint blocksPerPlane = BlockCount.x * BlockCount.y;
        uint blockZ = blockIndex / blocksPerPlane;
        uint planeIndex = blockIndex % blocksPerPlane;
        uint blockY = planeIndex / BlockCount.x;
        uint blockX = planeIndex % BlockCount.x;
        float3 blockMinimum = BoundsMin +
            float3(blockX, blockY, blockZ) * StorageBlockSize;

        for (uint candidateIndex = 0;
             candidateIndex < CandidateCount;
             ++candidateIndex)
        {
            float3 position =
                CandidatePoint(blockMinimum, candidateIndex);
            if (QueryVoxel(position) ||
                InsideAlwaysIncludeVolume(position))
            {
                Probes.Append(position);
            }
        }
    }
}
