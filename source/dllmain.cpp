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
