
StructuredBuffer<int> srcBuffer: register(t0);	    // SRV
RWStructuredBuffer<int> dstBuffer: register(u0);	// UAV

[numthreads(1024, 1, 1)]
void CSMain(uint3 groupID : SV_GroupID, uint3 tid : SV_DispatchThreadID, uint3 localTID : SV_GroupThreadID, uint groupIndex : SV_GroupIndex)
{
    const int index = tid.x;

    dstBuffer[index] = srcBuffer[index] + 10;
}
