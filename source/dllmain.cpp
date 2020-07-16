// dllmain.cpp : Defines the entry point for the DLL application.
#include "StdInc.h"
#include <ctime>
#include<sstream>
void InjectHooksMain(void);

void DisplayConsole(void)
{
    if (AllocConsole())
    {
        freopen("CONIN$", "r", stdin);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

inline bool isKeyPressed(unsigned int keyCode) {
    return (GetKeyState(keyCode) & 0x8000) != 0;
}

std::string GetStr(int intValue)
{
    std::stringstream sstream;
    sstream << intValue;
    return sstream.str();
}

static void PrintTasks(CPed* ped)
{
    printf("ped modelId = %d | primary tasks:\n", ped->m_nModelIndex);
    CTaskManager& taskManager = ped->GetTaskManager();
    for (std::int32_t i = 0; i < TASK_PRIMARY_MAX; i++) {
        CTask* pTask = taskManager.GetPrimaryTask(i);
        if (pTask) {
            std::string str = "primaryTask ";
            str = str + GetStr(i) + ": " + GetStr(pTask->GetId()) + ", ";
            if (!pTask->IsSimple()) {
                for (CTask* pSubTask = pTask->GetSubTask(); pSubTask; pSubTask = pSubTask->GetSubTask()) {
                    str = str + GetStr(pSubTask->GetId()) + ", ";
                    if(pSubTask->IsSimple())
                        break;
                }
            }
            printf("%s\n", str.c_str());
        }
    }
}

static bool IsEntityInRadius(CEntity* entity, CVector& point, float radius) {
    return (point - entity->GetPosition()).SquaredMagnitude() < radius * radius;
}

static void OnTrainStartsMoving(CPlayerPed* pPlayer, CVector& cjSpawnPoint) {
    short entitiesFound = 0;
    CEntity* entity = nullptr;
    bool bVehicles = true;
    CWorld::FindObjectsOfTypeInRange(MODEL_SANCHEZ, cjSpawnPoint, 10.0f, false, &entitiesFound, 1, &entity, false, bVehicles, false, false, false);
    if (entitiesFound > 0) {
        CBike* bike = static_cast<CBike*>(entity);
        bool bPeds = true;
        CWorld::FindObjectsOfTypeInRange(MODEL_SPECIAL01, cjSpawnPoint, 10.0f, false, &entitiesFound, 1, &entity, false, false, bPeds, false, false);
        if (entitiesFound > 0) {
            CPed* bigSmoke = static_cast<CPed*>(entity);
            bigSmoke->GetTaskManager().SetTask(new CTaskComplexEnterCarAsDriver(bike), TASK_PRIMARY_PRIMARY);
            bigSmoke->CantBeKnockedOffBike = 1; // never knock off ped
        }
        pPlayer->GetPadFromPlayer()->bPlayerSafe = true;
        pPlayer->GetTaskManager().SetTask(new CTaskComplexEnterCarAsPassenger(bike, 0, false), TASK_PRIMARY_PRIMARY);
        pPlayer->CantBeKnockedOffBike = 1; // never knock off ped

    }
}

DWORD GetTotalBlockSize(DWORD blockSize)
{
    const uint64_t IMG_BLOCK_SIZE = 2048;
    blockSize = (blockSize + IMG_BLOCK_SIZE - 1) & ~(IMG_BLOCK_SIZE - 1);            // Round up to block size
    return blockSize / IMG_BLOCK_SIZE;
}
static bool LoadVehicleRecording(const char* fileName, int& recordingId)
{
    FILE* file = fopen(fileName, "rb");
    if (file) {
        if (fseek(file, 0L, SEEK_END) == 0) {
            int fileSize = ftell(file);
            if (fileSize != -1L) {
                if (fseek(file, 0L, SEEK_SET) == 0) {
                    printf("file size is %d\n", fileSize);
                    recordingId = CVehicleRecording::RegisterRecordingFile(fileName);
                    printf("recording id = %d\n", recordingId);
                    std::vector<std::int8_t> buffer;
                    buffer.resize(fileSize);
                    if (fread(buffer.data(), 1, fileSize, file) == fileSize) {
                        RwMemory rwmem;
                        rwmem.start = (RwUInt8*)buffer.data();
                        rwmem.length = fileSize;
                        RwStream* stream = RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &rwmem);
                        bool bLoaded = false;
                        if (stream) {
                            CVehicleRecording::Load(stream, recordingId, fileSize); // Load will close the rw stream
                            bLoaded = true;
                        }
                        fclose(file);
                        if (bLoaded)
                            return true;
                    }
                    else
                    {
                        printf("fread failed for file '%s'\n", fileName);
                    }
                }
                else {
                    printf("fseek (SEEK_SET) failed for file '%s'\n", fileName);
                }
            }
            else {
                printf("ftell failed for file '%s'\n", fileName);
            }
        }
        else {
            printf("fseek (SEEK_END) failed for file '%s'\n", fileName);
        }
        fclose(file);
    }
    else {
        printf("failed to open file '%s'\n", fileName);
    }
    return false;
}

static void PlayMissionVehicleRecording(CVehicle* vehicle, const char* fileName)
{
    int rrrNumber = -1;
    if (!sscanf(fileName, "carrec%d", &rrrNumber))
        sscanf(fileName, "CARREC%d", &rrrNumber);
    if (rrrNumber == -1) {
        printf("Failed to read rrr number for file '%s'\n", fileName);
        return;
    }
    if (!CVehicleRecording::HasRecordingFileBeenLoaded(rrrNumber)) {
        printf("Attempting to load file '%s'\n", fileName);
        if (CVehicleRecording::NumPlayBackFiles >= TOTAL_RRR_MODEL_IDS) {
            printf("Failed to load '%s'. All RRR slots are being used\n", fileName);
            return;
        }
        int recordingId = 0;
        if (!LoadVehicleRecording(fileName, recordingId)) {
            printf("Failed to load file '%s'\n", fileName);
            return;
        }
    }
    CVehicleRecording::StartPlaybackRecordedCar(vehicle, rrrNumber, 0, 0);
}

enum class eMissionState
{
    NONE,
    TRAIN_MOVING,
    DRIVEBY
};
static enum class eMissionState missionState = eMissionState::NONE;

void WINAPI DllThread(void)
{
    RwCamera* pRwCamera = *(RwCamera**)0xC1703C;

    // Fail if RenderWare has already been started
    if (pRwCamera)
    {
        // MessageBox(NULL, "gta_revered failed to load (RenderWare has already been started)", "Error", MB_ICONERROR | MB_OK);
        // return;
    }

    DisplayConsole();
    //InjectHooksMain();
    clock_t OnePressTMR = clock();
    clock_t wideScreenTMR = clock();
    while (1)
    {
        if (clock() - OnePressTMR > 400) {
            if (isKeyPressed('3'))
            {
                OnePressTMR = clock();
                CPlayerPed* pPlayer = FindPlayerPed(-1);
                if (pPlayer && pPlayer->m_pVehicle) {
                    PlayMissionVehicleRecording(pPlayer->m_pVehicle, "carrec968.rrr");
                    //CVehicleRecording::StopPlaybackRecordedCar(v20);
                    // CTheScripts::CleanUpThisVehicle(v26); 
                }
            }
        }

        if (CTheScripts::OnAMissionFlag && !TheCamera.m_bWideScreenOn) {
            if (clock() - wideScreenTMR > 400)
            {
                wideScreenTMR = clock();
                CPlayerPed* pPlayer = FindPlayerPed(-1);
                switch (missionState)
                {
                case eMissionState::NONE:
                {
                    // okay, cutscene has ended
                    // The position where CJ is spawned after the train starts moving
                    CVector cjSpawnPoint(1774.0649f, -1943.0031f, 13.5587f);
                    if (IsEntityInRadius(pPlayer, cjSpawnPoint, 5.0f)) {
                        missionState = eMissionState::TRAIN_MOVING;
                        OnTrainStartsMoving(pPlayer, cjSpawnPoint);
                    }
                    break;
                }
                case eMissionState::TRAIN_MOVING:
                {
                    if (pPlayer->IsInVehicleThatHasADriver()) {
                        CPad* playerPad = pPlayer->GetPadFromPlayer();
                        playerPad->bPlayerSafe = false;
                        playerPad->bDisablePlayerEnterCar = true;
                        CVector targetPoint(0.0f, 0.0f, 0.0f);
                        pPlayer->GetTaskManager().SetTask(
                            new CTaskSimpleGangDriveBy(nullptr, &targetPoint, 300.0f, 100, 8, true), TASK_PRIMARY_PRIMARY);

                        //CVehicle* vehicle = pPlayer->m_pVehicle;
                        //CPed* driver = vehicle->m_pDriver;
                        missionState = eMissionState::DRIVEBY;
                    }
                    break;
                }
                }
            }
        }
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)DllThread, NULL, NULL, NULL);
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
