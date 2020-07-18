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

const char* WRONG_SIDE_OF_THE_TRACKS_MISSION_NAME = "smoke3";

static CRunningScript* GetMissionScript(const char* name)
{
    for (auto script = CTheScripts::pActiveScripts; script; script = script->m_pNext) {
        if (script->m_bIsMission && !strcmp(script->m_szName, name)) {
            return script;
        }
    }
    return nullptr;
}

static bool IsRunningWrongSideOfTheTracksMission()
{
    if (CTheScripts::IsPlayerOnAMission() && GetMissionScript(WRONG_SIDE_OF_THE_TRACKS_MISSION_NAME))
        return true;
    return false;
}

// OPCODE: 074A and 0709 (yes, both opcodes behave exactly the same)
static void AddDecisionMakerEventResponse(std::int32_t decisionMakerId, std::int32_t eventType, std::int32_t taskId,
    float respect, float hate, float like, float dislike, bool inCar, bool onFoot)
{
    float responseChances[4];
    responseChances[2] = respect;
    responseChances[3] = hate;
    responseChances[1] = like;
    responseChances[0] = dislike;

    std::int32_t flags[2];
    flags[1] = inCar;
    flags[0] = onFoot;
    CDecisionMakerTypes::GetInstance()->AddEventResponse(decisionMakerId, eventType, taskId, responseChances, flags);
}

// OPCODE: 0688
static void TogglePedThreadScanner(CPed* ped, bool bScanAllowedScriptPed, bool bScanAllowedInVehicle, bool bScanAllowedScriptedTask)
{
    if (ped) {
        CTaskSimpleTogglePedThreatScanner task(bScanAllowedScriptPed, bScanAllowedInVehicle, bScanAllowedScriptedTask);
        task.ProcessPed(ped);
    }
    else {
        CTask* task = new CTaskSimpleTogglePedThreatScanner(bScanAllowedScriptPed, bScanAllowedInVehicle, bScanAllowedScriptedTask);
        CTaskSequences::AddTaskToActiveSequence(task);
    }
}

// OPCODE: 060B
static void SetPedDecisionMaker(CPed* ped, std::int32_t decisionMakerId = -1)
{
    ped->GetIntelligence()->SetPedDecisionMakerType(decisionMakerId);
}

// OPCODE: 06AD
static void SetGroupDecisionMaker(CPedGroup& pedGroup, std::int32_t decisionMakerId = -1)
{
    pedGroup.GetIntelligence().SetGroupDecisionMakerType(decisionMakerId);
}

// OPCODE: 077A
static void SetPedRelationship(CPed* ped, std::int32_t acquaintanceID, ePedType pedType)
{
    ped->m_acquaintance.SetAsAcquaintance(acquaintanceID, CPedType::GetPedFlag(pedType));
}

// OPCODE: 0749
static void ClearGroupDecisionMakerEventResponse(std::int32_t decisionMakerId, eEventType eventId)
{
    if (decisionMakerId >= 0 && decisionMakerId < TOTAL_DECISION_MAKERS)
        CDecisionMakerTypes::GetInstance()->FlushDecisionMakerEventResponse(decisionMakerId, eventId);
}

// OPCODE: 060A
static std::int32_t LoadPedDecisionMaker(std::int32_t decisionMakerNameIndex, CRunningScript* script)
{
    char pedDMName[256];
    CDecisionMakerTypesFileLoader::GetPedDMName(decisionMakerNameIndex, pedDMName);
    std::int32_t decisionMakerId = CDecisionMakerTypesFileLoader::LoadDecisionMaker(pedDMName, 0, script->m_bUseMissionCleanup);
    std::int32_t decisionMakerScriptIndex = CTheScripts::GetNewUniqueScriptThingIndex(decisionMakerId, SCRIPT_THING_DECISION_MAKER);
    if (script->m_bUseMissionCleanup)
        CTheScripts::MissionCleanUp.AddEntityToList(decisionMakerScriptIndex, MISSION_CLEANUP_ENTITY_TYPE_DECISION_MAKER);
    return decisionMakerId;
}

// OPCODE: 06AE
static std::int32_t LoadGroupDecisionMaker(std::int32_t decisionMakerNameIndex, CRunningScript* script)
{
    char grpDMName[256];
    CDecisionMakerTypesFileLoader::GetGrpDMName(decisionMakerNameIndex, grpDMName);
    std::int32_t decisionMakerId = CDecisionMakerTypesFileLoader::LoadDecisionMaker(grpDMName, 1, script->m_bUseMissionCleanup);
    std::int32_t decisionMakerScriptIndex = CTheScripts::GetNewUniqueScriptThingIndex(decisionMakerId, SCRIPT_THING_DECISION_MAKER);
    if (script->m_bUseMissionCleanup)
        CTheScripts::MissionCleanUp.AddEntityToList(decisionMakerScriptIndex, MISSION_CLEANUP_ENTITY_TYPE_DECISION_MAKER);
    return decisionMakerId;
}

// OPCODE: 0631
static bool SetGroupMember(std::int32_t groupId, CPed* ped)
{
    if (groupId >= 0 && groupId < TOTAL_PED_GROUPS) {
        CTask* pTaskComplexBeInGroup = new CTaskComplexBeInGroup(groupId, false);
        CEventScriptCommand eventScriptCommand(TASK_PRIMARY_PRIMARY, pTaskComplexBeInGroup, false);
        ped->GetEventGroup().Add(&eventScriptCommand, false);
        CPedGroup& pedGroup = CPedGroups::GetGroup(groupId);
        CPedGroupMembership& groupMembership = pedGroup.GetMembership();
        if (groupMembership.CountMembersExcludingLeader() >= 7) {
            if (groupMembership.GetLeader() && groupMembership.GetLeader()->IsPlayer()) 
                groupMembership.RemoveNFollowers(1);
        }
        groupMembership.AddFollower(ped);
        pedGroup.Process();
        // This is where we should check if the leader is in a vehicle, but it's enough for the train mission
        return true;
    }
    return false;
}

// OPCODE: 09DD
static void MakeRoomInPlayerGroup(std::int32_t spaceRequired)
{
    std::uint8_t scriptLimitToGangSize = FindPlayerPed()->m_pPlayerData->m_nScriptLimitToGangSize;
    CPedGroupMembership& groupMembership = FindPlayerPed()->GetGroup().GetMembership();
    std::int32_t followersToRemove = groupMembership.CountMembersExcludingLeader() - scriptLimitToGangSize - spaceRequired;
    if (followersToRemove > 0) 
        groupMembership.RemoveNFollowers(followersToRemove);
}

static void SetPedAsPlayerFollower(CPlayerPed* player, CPed* ped)
{
    CPedGroup& playerGroup = player->GetGroup();
    CRunningScript* script = GetMissionScript(WRONG_SIDE_OF_THE_TRACKS_MISSION_NAME);
    std::int32_t pedDecisionMakerId = LoadPedDecisionMaker(0, script); // decision\allowed\m_.ped files
    //SetPedDecisionMaker(ped, pedDecisionMakerId);
    MakeRoomInPlayerGroup(1);
    SetGroupMember(player->GetGroupId(), ped);
    playerGroup.m_fSeparationRange = 30.0f;
    std::int32_t groupDecisionMakerId = LoadGroupDecisionMaker(0, script); // decision\allowed\mission.grp
    ClearGroupDecisionMakerEventResponse(groupDecisionMakerId, EVENT_DAMAGE);
    ClearGroupDecisionMakerEventResponse(groupDecisionMakerId, EVENT_VEHICLE_DAMAGE_WEAPON);
    ClearGroupDecisionMakerEventResponse(groupDecisionMakerId, EVENT_ACQUAINTANCE_PED_HATE);
    SetGroupDecisionMaker(playerGroup, groupDecisionMakerId);
    SetPedRelationship(ped, 4, PED_TYPE_MISSION1);
    SetPedDecisionMaker(ped, pedDecisionMakerId);
    TogglePedThreadScanner(ped, false, true, false);
    AddDecisionMakerEventResponse(groupDecisionMakerId, EVENT_ACQUAINTANCE_PED_HATE, TASK_GROUP_USE_MEMBER_DECISION, 0.0f, 100.0f, 0.0f, 0.0f, true, false);
    AddDecisionMakerEventResponse(0, EVENT_ACQUAINTANCE_PED_HATE, TASK_SIMPLE_GANG_DRIVEBY, 0.0f, 100.0f, 0.0f, 0.0f, true, false);
    player->ForceGroupToAlwaysFollow(true);

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

static CPed* GetNearestMissionEnemy(CPlayerPed* player, std::vector<CPed*>& enemies)
{
    float nearDistance = 100000.0f;
    CPed* ped = nullptr;
    for (size_t i = 0; i < enemies.size(); i++) {
        CPed* enemy = enemies[i];
        if (enemy) {
            float distance = DistanceBetweenPoints(enemy->GetPosition(), player->GetPosition());
            if (distance < nearDistance) {
                nearDistance = distance;
                ped = enemy;
            }
        }
    }
    return ped;
}

static bool IsPedMissionEnemy(CPed* ped)
{
    std::int32_t modelId = ped->m_nModelIndex;
    if (modelId == MODEL_LSV1 || modelId == MODEL_LSV2 || modelId == MODEL_LSV3) {
        if (ped->m_nCreatedBy == PED_MISSION && ped->m_pAttachedTo && ped->m_pAttachedTo->m_nType == ENTITY_TYPE_VEHICLE)
            return true;
    }
    return false;
}

static void GetPlayerEnemies(CPlayerPed* pPlayer, float radius, std::vector<CPed*>& enemies)
{
    for (int i = CPools::ms_pPedPool->GetSize() - 1; i >= 0; i--) {
        CPed* pPed = CPools::ms_pPedPool->GetAt(i);
        if (pPed) {
            if (IsEntityInRadius(pPed, pPlayer->GetPosition(), radius) && IsPedMissionEnemy(pPed)) {
                if (enemies.size() == enemies.capacity())
                    break;
                enemies.push_back(pPed);
            }
        }
    }
    printf("Found %d enemies\n", enemies.size());
}

static void UpdateDriveByTargetEnemy(CPlayerPed* player, std::vector<CPed*>& enemies)
{
    CPed* enemy = GetNearestMissionEnemy(player, enemies);
    if (enemy) {
        eWeaponType weaponType = player->GetActiveWeapon().m_nType;
        auto skill = player->GetWeaponSkill(weaponType);
        auto weaponInfo = CWeaponInfo::GetWeaponInfo(weaponType, skill);
        weaponInfo->m_fWeaponRange = 7.0f;
        printf("m_fTargetRange, >m_fWeaponRange = %f, %f | skill = %d\n", weaponInfo->m_fTargetRange, weaponInfo->m_fWeaponRange, skill);
        /*auto task = static_cast<CTaskSimpleGangDriveBy*>(player->GetTaskManager().FindTaskByType(TASK_PRIMARY_PRIMARY, TASK_SIMPLE_GANG_DRIVEBY));
        if (task) {
            task->m_pTargetEntity = enemy;
        }*/
    }

}

static void OnTrainStartsMoving(CPlayerPed* pPlayer, CVector& cjSpawnPoint, std::vector<CPed*>& enemies, CBike**pOutBike) {
    CWanted::SetMaximumWantedLevel(0);
    short entitiesFound = 0;
    CEntity* entity = nullptr;
    bool bVehicles = true;
    CWorld::FindObjectsOfTypeInRange(MODEL_SANCHEZ, cjSpawnPoint, 10.0f, false, &entitiesFound, 1, &entity, false, bVehicles, false, false, false);
    if (entitiesFound > 0) {
        CBike* bike = static_cast<CBike*>(entity);
        *pOutBike = bike;
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
        pPlayer->GiveWeapon(WEAPON_TEC9, 99999, 0);
        //pPlayer->SetCurrentWeapon(32);
    }
}

static bool LoadVehicleRecording(const char* fileName, std::int32_t recordingId)
{
    FILE* file = fopen(fileName, "rb");
    if (file) {
        if (fseek(file, 0L, SEEK_END) == 0) {
            int fileSize = ftell(file);
            if (fileSize != -1L) {
                if (fseek(file, 0L, SEEK_SET) == 0) {
                    printf("file size is %d\n", fileSize);
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

static bool PlayMissionVehicleRecording(CVehicle* vehicle, const char* fileName, std::int32_t& recordingId, std::int32_t& rrrNumber)
{
    rrrNumber = -1;
    if (!sscanf(fileName, "carrec%d", &rrrNumber))
        sscanf(fileName, "CARREC%d", &rrrNumber);
    if (rrrNumber == -1) {
        printf("Failed to read rrr number for file '%s'\n", fileName);
        return false;
    }
    if (recordingId == -1) {
        if (CVehicleRecording::NumPlayBackFiles >= TOTAL_RRR_MODEL_IDS) {
            printf("Failed to load '%s'. All RRR slots are being used\n", fileName);
            return false;
        }
        recordingId = CVehicleRecording::RegisterRecordingFile(fileName);
        printf("recording id = %d\n", recordingId);
    }
    printf("Attempting to load file '%s'\n", fileName);
    if (!LoadVehicleRecording(fileName, recordingId)) {
        printf("Failed to load file '%s'\n", fileName);
        return false;
    }
    CVehicleRecording::StartPlaybackRecordedCar(vehicle, rrrNumber, 0, 0);
    return true;
}

static void ProcessAliveEnemies(std::vector<CPed*>& enemies, std::int32_t& totalAliveEnemies)
{
    totalAliveEnemies = 0;
    for (size_t i = 0; i < enemies.size(); i++) {
        CPed* enemy = enemies[i];
        if (enemy) {
            if (enemy->IsAlive() && enemy->m_pAttachedTo && enemy->m_pAttachedTo->m_nType == ENTITY_TYPE_VEHICLE)
                totalAliveEnemies++;
            else
                enemies[i] = nullptr;
        }
    }
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

static void Mission_Process()
{
    CFont::InitPerFrame(); // we replaced the call to this function in hook, so let's call it first

    static clock_t loopTimer = clock();
    static CBike* bike = nullptr;
    static CPed* bigSmoke = nullptr;
    static std::int32_t rrrNumber = -1;
    static std::int32_t vehicleRecordingId = -1;
    static std::vector<CPed*> enemies;
    bool bRunningWrongSideOfTheTracksMission = IsRunningWrongSideOfTheTracksMission();
    if (!bRunningWrongSideOfTheTracksMission && missionState != eMissionState::NONE) {
        missionState = eMissionState::NONE;
        bike = nullptr;
        bigSmoke = nullptr;
        enemies.resize(0);
        return;
    }
    if (bRunningWrongSideOfTheTracksMission && TheCamera.m_bWideScreenOn && missionState != eMissionState::NONE
        && missionState != eMissionState::DRIVING_TO_SMOKES_HOUSE && missionState != eMissionState::FAILED) {
        // cutscene has started, this means the train will now go out of sight
        // yes, this also means we lost the mission
        CPlayerPed* pPlayer = FindPlayerPed(-1);
        if (pPlayer->m_pVehicle) {
            CVehicleRecording::StopPlaybackRecordedCar(pPlayer->m_pVehicle);
            pPlayer->CantBeKnockedOffBike = 0;
            CPed* bigSmoke = pPlayer->m_pVehicle->m_pDriver;
            if (bigSmoke) {
                bigSmoke->CantBeKnockedOffBike = 0;
                SetPedAsPlayerFollower(pPlayer, bigSmoke);
            }
            missionState = eMissionState::FAILED;
        }
    }

    if (clock() - loopTimer > 100 && bRunningWrongSideOfTheTracksMission) {
        loopTimer = clock();
        CPlayerPed* pPlayer = FindPlayerPed(-1);
        if (!TheCamera.m_bWideScreenOn &&
            missionState != eMissionState::NONE && enemies.size() > 0) {
            std::int32_t totalAliveEnemies = 0;
            ProcessAliveEnemies(enemies, totalAliveEnemies);
            if (totalAliveEnemies == 0) {
                missionState = eMissionState::ALL_ENEMIES_ARE_DEAD;
                enemies.resize(0);
            }
        }

        UpdateDriveByTargetEnemy(pPlayer, enemies);

        switch (missionState)
        {
        case eMissionState::NONE:
        {
            if (!TheCamera.m_bWideScreenOn) {
                // okay, cutscene has ended
                // The position where CJ is spawned after the train starts moving
                CVector cjSpawnPoint(1774.0649f, -1943.0031f, 13.5587f);
                if (IsEntityInRadius(pPlayer, cjSpawnPoint, 5.0f)) {
                    std::vector<CPed*> missionEnemies;
                    missionEnemies.reserve(4);
                    GetPlayerEnemies(pPlayer, 150.0f, missionEnemies);
                    if (missionEnemies.size() >= 4) { // check if all 4 enemies have spawned and are attached to train
                        OnTrainStartsMoving(pPlayer, cjSpawnPoint, enemies, &bike);
                        if (bike)
                            missionState = eMissionState::TRAIN_MOVING;
                    }
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
                pPlayer->GetTaskManager().SetTask(
                    new CTaskSimpleGangDriveBy(nullptr, &targetPoint, 200.0f, 30, 8, true), TASK_PRIMARY_PRIMARY);
                if (PlayMissionVehicleRecording(pPlayer->m_pVehicle, "carrec968.rrr", vehicleRecordingId, rrrNumber)) {
                    CVehicleRecording::SetPlaybackSpeed(pPlayer->m_pVehicle, 0.8f);
                    missionState = eMissionState::BIKE_MOVING;
                }
            }
            break;
        }
        case eMissionState::ALL_ENEMIES_ARE_DEAD:
        {
            if (pPlayer->IsInVehicleThatHasADriver()) {
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
                    SetPedAsPlayerFollower(pPlayer, bigSmoke);
                     //bigSmoke->GetTaskManager().SetTask(new CTaskComplexEnterCarAsPassenger(bike, 0, false), TASK_PRIMARY_PRIMARY);
                    pPlayer->GetTaskManager().SetTask(new CTaskComplexEnterCarAsDriver(bike), TASK_PRIMARY_PRIMARY);
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
