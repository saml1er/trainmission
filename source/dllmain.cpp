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

static void MakeVehicleInvulnerable(CVehicle* vehicle, bool invulnerable)
{
    vehicle->physicalFlags.bBulletProof = invulnerable;
    vehicle->physicalFlags.bExplosionProof = invulnerable;
    vehicle->physicalFlags.bInvulnerable = invulnerable;
    vehicle->physicalFlags.bFireProof = invulnerable;
}

static void GetPlayerEnemies(CPlayerPed* pPlayer, float radius, std::vector<CPed*>& enemies)
{
    for (int i = CPools::ms_pPedPool->GetSize() - 1; i >= 0; i--) {
        CPed* pPed = CPools::ms_pPedPool->GetAt(i);
        if (pPed) {
            if (IsEntityInRadius(pPed, pPlayer->GetPosition(), radius) &&
                (pPed->m_nModelIndex == MODEL_LSV1 || pPed->m_nModelIndex == MODEL_LSV2 || pPed->m_nModelIndex == MODEL_LSV3)) {
                if (enemies.size() == enemies.capacity())
                    break;
                enemies.push_back(pPed);
            }
        }
    }
    printf("Found %d enemies\n", enemies.size());
}

static void OnTrainStartsMoving(CPlayerPed* pPlayer, CVector& cjSpawnPoint, std::vector<CPed*>& enemies) {
    CWanted::SetMaximumWantedLevel(0);
    short entitiesFound = 0;
    CEntity* entity = nullptr;
    bool bVehicles = true;
    CWorld::FindObjectsOfTypeInRange(MODEL_SANCHEZ, cjSpawnPoint, 10.0f, false, &entitiesFound, 1, &entity, false, bVehicles, false, false, false);
    if (entitiesFound > 0) {
        CBike* bike = static_cast<CBike*>(entity);
        MakeVehicleInvulnerable(bike, true);
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

        enemies.reserve(4);
        GetPlayerEnemies(pPlayer, 150.0f, enemies);
    }
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
        CVehicleRecording::StreamingArray[recordingId].m_nRefCount++; // so it always stays in memory
    }
    CVehicleRecording::StartPlaybackRecordedCar(vehicle, rrrNumber, 0, 0);
}

enum class eMissionState
{
    NONE,
    TRAIN_MOVING,
    ALL_ENEMIES_ARE_DEAD,
    BIKE_MOVING,
    BIKE_STOPPED,
    GETTING_OUT_OF_BIKE,
    GETTING_BACK_ON_BIKE,
    DRIVING_TO_SMOKES_HOUSE,
    FAILED
};
static enum class eMissionState missionState = eMissionState::NONE;

static bool IsRunningWrongSideOfTheTracksMission()
{
    if (CTheScripts::IsPlayerOnAMission()) {
        for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext) {
            if (script->m_bIsMission && !strcmp(script->m_szName, "smoke3")) {
                return true;
            }
        }
    }
    return false;
}

static void Mission_Process()
{
    CFont::InitPerFrame(); // we replaced the call to this function in hook, so let's call it first
    static clock_t loopTimer = clock();
    static CVehicle* bike = nullptr;
    static CPed* bigSmoke = nullptr;
    static std::vector<CPed*> enemies;
    bool bRunningWrongSideOfTheTracksMission = IsRunningWrongSideOfTheTracksMission();
    if (!bRunningWrongSideOfTheTracksMission && missionState != eMissionState::NONE) {
        missionState = eMissionState::NONE;
        bike = nullptr;
        bigSmoke = nullptr;
        enemies.resize(0);
        return;
    }
    if (clock() - loopTimer > 100 && bRunningWrongSideOfTheTracksMission) {
        loopTimer = clock();
        CPlayerPed* pPlayer = FindPlayerPed(-1);
        if (!TheCamera.m_bWideScreenOn &&
            missionState != eMissionState::NONE && enemies.size() > 0) {
            std::int32_t totalAliveEnemies = 0;
            for (size_t i = 0; i < enemies.size(); i++) {
                CPed* enemy = enemies[i];
                if (enemy) {
                    if (enemy->IsAlive() && enemy->m_pAttachedTo && enemy->m_pAttachedTo->m_nType == ENTITY_TYPE_VEHICLE)
                        totalAliveEnemies++;
                    else
                        enemies[i] = nullptr;
                }
            }
            if (totalAliveEnemies == 0) {
                missionState = eMissionState::ALL_ENEMIES_ARE_DEAD;
                enemies.resize(0);
            }
        }

        if (TheCamera.m_bWideScreenOn && missionState != eMissionState::NONE
            && missionState != eMissionState::DRIVING_TO_SMOKES_HOUSE && missionState != eMissionState::FAILED) {
            // cutscene has started, this means the train will now go out of sight
            // yes, this also means we lost the mission
            if (pPlayer->m_pVehicle) {
                CVehicleRecording::StopPlaybackRecordedCar(pPlayer->m_pVehicle);
                pPlayer->CantBeKnockedOffBike = 0;
                CPed* bigSmoke = pPlayer->m_pVehicle->m_pDriver;
                if (bigSmoke)
                    bigSmoke->CantBeKnockedOffBike = 0;
                missionState = eMissionState::FAILED;
            }
        }

        switch (missionState)
        {
        case eMissionState::NONE:
        {
            if (!TheCamera.m_bWideScreenOn) {
                // okay, cutscene has ended
                // The position where CJ is spawned after the train starts moving
                CVector cjSpawnPoint(1774.0649f, -1943.0031f, 13.5587f);
                if (IsEntityInRadius(pPlayer, cjSpawnPoint, 5.0f)) {
                    missionState = eMissionState::TRAIN_MOVING;
                    OnTrainStartsMoving(pPlayer, cjSpawnPoint, enemies);
                }
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
                pPlayer->GiveWeapon(WEAPON_TEC9, 99999, 0);
                //pPlayer->SetCurrentWeapon(32);
                pPlayer->GetTaskManager().SetTask(
                    new CTaskSimpleGangDriveBy(nullptr, &targetPoint, 300.0f, 70, 8, true), TASK_PRIMARY_PRIMARY);
                PlayMissionVehicleRecording(pPlayer->m_pVehicle, "carrec968.rrr");
                CVehicleRecording::SetPlaybackSpeed(pPlayer->m_pVehicle, 0.8f);
                missionState = eMissionState::BIKE_MOVING;
            }
            break;
        }
        case eMissionState::ALL_ENEMIES_ARE_DEAD:
        {
            if (pPlayer->IsInVehicleThatHasADriver()) {
                bike = pPlayer->m_pVehicle;
                bigSmoke = pPlayer->m_pVehicle->m_pDriver;
                CVehicleRecording::StopPlaybackRecordedCar(bike);
                missionState = eMissionState::BIKE_STOPPED;
            }
            break;
        }
        case eMissionState::BIKE_STOPPED:
        {
            if (bike && CTheScripts::IsVehicleStopped(bike)) {
                pPlayer->CantBeKnockedOffBike = 0;
                bigSmoke->CantBeKnockedOffBike = 0;
                pPlayer->GetTaskManager().SetTask(
                    new CTaskComplexLeaveCar(pPlayer->m_pVehicle, 0, 0, true, false), TASK_PRIMARY_PRIMARY);
                bigSmoke->GetTaskManager().SetTask(
                    new CTaskComplexLeaveCar(pPlayer->m_pVehicle, 0, 0, true, false), TASK_PRIMARY_PRIMARY);
                missionState = eMissionState::GETTING_OUT_OF_BIKE;
            }
            break;
        }
        case eMissionState::GETTING_OUT_OF_BIKE:
        {
            if (bike && bigSmoke) {
                if (!pPlayer->bInVehicle && !bigSmoke->bInVehicle) {
                    pPlayer->GetTaskManager().SetTask(new CTaskComplexEnterCarAsDriver(bike), TASK_PRIMARY_PRIMARY);
                    bigSmoke->GetTaskManager().SetTask(new CTaskComplexEnterCarAsPassenger(bike, 0, false), TASK_PRIMARY_PRIMARY);
                    missionState = eMissionState::GETTING_BACK_ON_BIKE;
                }
                }
            break;
        }
        case eMissionState::GETTING_BACK_ON_BIKE:
        {
            CVehicle* vehicle = pPlayer->m_pVehicle;
            if (vehicle && vehicle->m_pDriver) {
                MakeVehicleInvulnerable(bike, false);
                pPlayer->GetPadFromPlayer()->bDisablePlayerEnterCar = false;
                CWanted::SetMaximumWantedLevel(4);
                missionState = eMissionState::DRIVING_TO_SMOKES_HOUSE;
            }
            break;
        }
        }
    }

}

DWORD RETURN_ADDRESS = 0x53E977;
void _declspec(naked) TheHook()
{
    __asm {
        call Mission_Process
        jmp RETURN_ADDRESS
    }
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisplayConsole();
        HookInstall(0x53E972, TheHook);
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
