#include "StdInc.h"

std::int32_t& CVehicleRecording::NumPlayBackFiles = *(std::int32_t*)0x97F630;

void CVehicleRecording::Load(RwStream *stream, int resourceId, int totalSize)
{
    return plugin::Call<0x45A8F0, RwStream*, int, int>(stream, resourceId, totalSize);
}

std::int32_t CVehicleRecording::RegisterRecordingFile(char const* name)
{
    return plugin::CallAndReturn<std::int32_t, 0x459F80, char const*>(name);
}

void CVehicleRecording::StartPlaybackRecordedCar(CVehicle* vehicle, int pathNumber, bool bUseCarAI, bool bLooped)
{
    plugin::Call<0x45A980, CVehicle*, int, bool, bool>(vehicle, pathNumber, bUseCarAI, bLooped);
}

void CVehicleRecording::StopPlaybackRecordedCar(CVehicle* vehicle)
{
    plugin::Call<0x45A280, CVehicle*>(vehicle);
}
