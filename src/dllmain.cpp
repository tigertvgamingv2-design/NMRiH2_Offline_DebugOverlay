#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SDK/Basic.hpp"
#include "SDK/CoreUObject_classes.hpp"
#include "SDK/Engine_classes.hpp"
#include "SDK/Kosovo_classes.hpp"
#include "SDK/BP_BasePickupFirearm_classes.hpp"
#include "SDK/BP_BasePickupMelee_classes.hpp"
#include "SDK/BP_Container_1D_Base_classes.hpp"
#include "SDK/BP_Player_classes.hpp"
#include "SDK/BP_Zombie_classes.hpp"
#include "SDK/BPA_VehicleParent_classes.hpp"

// Include WinUser only after the generated SDK has declared AHUD::DrawText.
// Then remove its DrawTextW/DrawTextA compatibility macro before our calls.
#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#ifndef OVERLAY_SOURCE_CHECK
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <shellapi.h>
#include <wrl/client.h>

#include "MinHook.h"
#endif

#if !defined(OVERLAY_SOURCE_CHECK) || defined(OVERLAY_UI_SOURCE_CHECK)
#include "imgui.h"
#endif

#ifndef OVERLAY_SOURCE_CHECK
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND Window,
    UINT Message,
    WPARAM WParam,
    LPARAM LParam);
#endif

namespace
{
using namespace SDK;

using ProcessEventFn = void(__fastcall*)(const UObject*, UFunction*, void*);

constexpr double kMinRadiusMeters = 25.0;
constexpr double kMaxRadiusMeters = 5000.0;
constexpr double kRadiusStepMeters = 25.0;
constexpr float kMinDamageMultiplier = 0.25F;
constexpr float kMaxDamageMultiplier = 20.0F;
constexpr float kDamageMultiplierStep = 0.25F;
constexpr float kOneHitMeleeMultiplier = 1000.0F;
constexpr float kMinAimFovPixels = 40.0F;
constexpr float kMaxAimFovPixels = 650.0F;
constexpr float kMinAimSpeed = 1.0F;
constexpr float kMaxAimSpeed = 30.0F;
constexpr float kMinSpeedMultiplier = 0.25F;
constexpr float kMaxSpeedMultiplier = 5.0F;
constexpr float kMinWeaponSpeedMultiplier = 0.25F;
constexpr float kMaxWeaponSpeedMultiplier = 5.0F;
constexpr float kMinGlobalTimeScale = 0.05F;
constexpr float kMaxGlobalTimeScale = 3.0F;
constexpr DWORD kHookPollMilliseconds = 500;
constexpr UINT64 kActorScanWarmupMilliseconds = 10000;
constexpr wchar_t kDiscordInvite[] = L"https://discord.gg/nKDN7vCFE7";

enum class DiagnosticStage : std::uint32_t
{
    Idle,
    WorkerStart,
    InstallDxHooks,
    PatchHud,
    ImGuiPresent,
    ImGuiInitialize,
    ImGuiFrame,
    ImGuiSubmit,
    ImGuiResize,
    ImGuiWindowProc,
    DxExecute,
    ExternalUiStart,
    ExternalUiFrame,
    ExternalUiResize,
    ActorWarmup,
    ActorSetup,
    ActorScan,
    ActorValidation,
    CharacterRender,
    ClassifyActor,
    ActorLocation,
    Unload
};

const char* DiagnosticStageName(DiagnosticStage Stage)
{
    switch (Stage)
    {
    case DiagnosticStage::Idle: return "Idle";
    case DiagnosticStage::WorkerStart: return "WorkerStart";
    case DiagnosticStage::InstallDxHooks: return "InstallDxHooks";
    case DiagnosticStage::PatchHud: return "PatchHud";
    case DiagnosticStage::ImGuiPresent: return "ImGuiPresent";
    case DiagnosticStage::ImGuiInitialize: return "ImGuiInitialize";
    case DiagnosticStage::ImGuiFrame: return "ImGuiFrame";
    case DiagnosticStage::ImGuiSubmit: return "ImGuiSubmit";
    case DiagnosticStage::ImGuiResize: return "ImGuiResize";
    case DiagnosticStage::ImGuiWindowProc: return "ImGuiWindowProc";
    case DiagnosticStage::DxExecute: return "DxExecute";
    case DiagnosticStage::ExternalUiStart: return "ExternalUiStart";
    case DiagnosticStage::ExternalUiFrame: return "ExternalUiFrame";
    case DiagnosticStage::ExternalUiResize: return "ExternalUiResize";
    case DiagnosticStage::ActorWarmup: return "ActorWarmup";
    case DiagnosticStage::ActorSetup: return "ActorSetup";
    case DiagnosticStage::ActorScan: return "ActorScan";
    case DiagnosticStage::ActorValidation: return "ActorValidation";
    case DiagnosticStage::CharacterRender: return "CharacterRender";
    case DiagnosticStage::ClassifyActor: return "ClassifyActor";
    case DiagnosticStage::ActorLocation: return "ActorLocation";
    case DiagnosticStage::Unload: return "Unload";
    default: return "Unknown";
    }
}

enum class AmmoKind : std::uint8_t
{
    None,
    NineMillimeter,
    FortyFiveAcp,
    TwentyTwoLr,
    ThreeFiftySeven,
    ThreeOhEight,
    FiveFiveSix,
    SixEightByFiftyOne,
    TwelveGauge
};

enum class MarkerCategory : std::uint8_t
{
    None,
    Firearm,
    Melee,
    Ammo,
    Medical,
    Explosive,
    Attachment,
    Equipment,
    Supply,
    Deployable,
    ObjectiveItem,
    MiscPickup,
    Container,
    Objective,
    Extraction,
    Interactable,
    Door,
    Vehicle,
    DebugActor,
    Count
};

enum class MenuTab : std::uint8_t
{
    General,
    Aim,
    Loot,
    Ammo,
    World,
    Damage,
    Misc,
    Tools,
    Theme,
    Inspector,
    Config,
    Count
};

enum class DamageModifierStatus : std::uint8_t
{
    Disabled,
    WaitingForPlayer,
    WaitingForAttributes,
    Applied,
    BlockedOnline
};

struct OverlaySettings
{
    double RadiusMeters{500.0};
    bool ShowPlayers{true};
    bool ShowZombies{true};
    bool ShowLoot{true};
    bool ShowFirearms{true};
    bool ShowMelee{true};
    bool ShowAmmo{true};
    bool ShowMedical{true};
    bool ShowExplosives{true};
    bool ShowAttachments{true};
    bool ShowEquipment{true};
    bool ShowSupplies{true};
    bool ShowDeployables{true};
    bool ShowObjectiveItems{true};
    bool ShowMiscPickups{true};
    bool Ammo9mm{true};
    bool Ammo45Acp{true};
    bool Ammo22Lr{true};
    bool Ammo357{true};
    bool Ammo308{true};
    bool Ammo556{true};
    bool Ammo68x51{true};
    bool Ammo12Gauge{true};
    bool ShowWorld{true};
    bool ShowContainers{true};
    bool ShowObjectives{true};
    bool ShowExtractions{true};
    bool ShowInteractables{true};
    bool ShowDoors{false};
    bool ShowVehicles{true};
    bool ShowDebugActors{false};
    bool ShowHealth{true};
    bool ShowHealthBars{true};
    bool ShowDistance{true};
    bool ShowNames{true};
    bool ShowClassNames{false};
    bool ShowStatus{true};
    bool ShowLegend{true};
    bool ShowSkeleton{false};
    bool ShowSnaplines{false};
    bool ShowHeadMarkers{true};
    bool ShowZombieTypeLabels{true};
    bool ShowRadar{true};
    bool RadarShowZombies{true};
    bool RadarShowLoot{true};
    bool RadarShowObjectives{true};
    float RadarRangeMeters{120.0F};
    float RadarSize{190.0F};
    bool ShowThreatIndicators{true};
    float ThreatRadiusMeters{40.0F};
    bool ShowWeaponInfo{true};
    bool ShowObjectiveTracker{true};
    bool ShowDiagnosticOverlay{false};
    int DiagnosticOverlayLines{7};
    bool EnableActorInspector{false};
    bool InspectorIncludeAllActors{true};
    int InspectorMaxEntries{750};
    bool PhotoMode{false};
    float PhotoTimeScale{0.15F};
    bool EnableZombieAimbot{false};
    bool DrawAimFov{true};
    bool AimVisibilityCheck{true};
    bool AimHipfireMode{false};
    bool EnableSimpleRcs{true};
    float AimFovPixels{220.0F};
    float AimSpeed{8.0F};
    float RcsStrength{0.65F};
    bool EnableOfflineDamageModifier{false};
    float OfflineDamageMultiplier{2.0F};
    bool IncludeStabilityDamage{true};
    bool OneHitMelee{false};
    bool DoubleMeleeRange{false};
    bool Fullbright{false};
    float FullbrightIntensity{3.0F};
    bool GhostMode{false};
    bool SpeedHack{false};
    float SpeedMultiplier{2.0F};
    int AmmoCheatMode{0};
    bool InfiniteStamina{false};
    bool NoRecoil{false};
    bool ModifyWeaponSpread{false};
    float WeaponSpreadMultiplier{0.0F};
    bool ModifyReloadSpeed{false};
    float ReloadSpeedMultiplier{2.0F};
    bool ModifyFireRate{false};
    float FireRateMultiplier{2.0F};
    bool GodMode{false};
    bool AutoHealthRegeneration{false};
    bool InfiniteFlashlightBattery{false};
    bool ModifyZombieTime{false};
    float ZombieTimeScale{0.0F};
    bool ModifyGlobalTime{false};
    float GlobalTimeScale{1.0F};
    int GiveAmmoAmount{100};
    int GiveSuppliesAmount{100};
    double PickupVacuumRadiusMeters{25.0};
    char AmmoGrantIdentifier[96]{};
    char SpawnItemIdentifier[160]{};
    float MenuAccent[4]{0.20F, 0.78F, 1.00F, 1.00F};
    float MenuBackground[4]{0.025F, 0.035F, 0.060F, 0.985F};
    float MenuPanel[4]{0.035F, 0.050F, 0.082F, 0.92F};
    float MenuText[4]{0.91F, 0.95F, 1.00F, 1.00F};
    float PlayerEspColor[4]{0.20F, 0.75F, 1.00F, 1.00F};
    float ZombieEspColor[4]{1.00F, 0.30F, 0.20F, 1.00F};
    float SkeletonColor[4]{1.00F, 0.92F, 0.40F, 1.00F};
    float HeadMarkerColor[4]{1.00F, 0.38F, 0.28F, 1.00F};
    float SnaplineColor[4]{0.50F, 0.82F, 1.00F, 0.80F};
    float ThreatColor[4]{1.00F, 0.18F, 0.12F, 1.00F};
    std::array<std::array<float, 4>, static_cast<std::size_t>(MarkerCategory::Count)> CategoryColors{{
        {0.70F, 0.70F, 0.70F, 1.00F}, {0.25F, 1.00F, 0.35F, 1.00F},
        {1.00F, 0.52F, 0.18F, 1.00F}, {1.00F, 0.82F, 0.18F, 1.00F},
        {0.15F, 1.00F, 0.82F, 1.00F}, {1.00F, 0.18F, 0.12F, 1.00F},
        {0.82F, 0.42F, 1.00F, 1.00F}, {0.30F, 0.62F, 1.00F, 1.00F},
        {0.45F, 1.00F, 0.62F, 1.00F}, {0.12F, 0.85F, 0.88F, 1.00F},
        {1.00F, 0.35F, 0.82F, 1.00F}, {0.80F, 0.84F, 0.90F, 1.00F},
        {0.35F, 0.82F, 1.00F, 1.00F}, {1.00F, 0.25F, 0.72F, 1.00F},
        {0.25F, 1.00F, 0.25F, 1.00F}, {0.95F, 0.95F, 1.00F, 1.00F},
        {0.76F, 0.52F, 0.30F, 1.00F}, {0.30F, 0.72F, 1.00F, 1.00F},
        {0.62F, 0.65F, 0.70F, 1.00F}
    }};
    bool RainbowAccent{false};
    float RainbowSpeed{0.20F};
    float MarkerSize{6.0F};
    float TextScale{0.9F};
    float BoxThickness{1.5F};
    int MaxMarkers{150};
    int MenuHotkey{VK_F1};
    int OverlayHotkey{VK_INSERT};
    int NextPageHotkey{VK_PRIOR};
    int UnloadHotkey{VK_END};
    int TeleKillSingleHotkey{VK_F6};
    int TeleKillAllHotkey{VK_F7};
    int SelectedProfile{0};
    MenuTab ActiveTab{MenuTab::General};
    int SelectedMenuRow{0};
};

struct ClassDescriptor
{
    MarkerCategory Category{MarkerCategory::None};
    AmmoKind Ammo{AmmoKind::None};
    std::string RawName;
    std::wstring FriendlyName;
};

struct MarkerCandidate
{
    FVector Location{};
    double Distance{};
    ClassDescriptor Descriptor{};
    AActor* Actor{};
};

struct RadarPoint
{
    FVector Location{};
    MarkerCategory Category{MarkerCategory::None};
    bool IsZombie{};
    double Distance{};
};

struct ThreatCandidate
{
    FVector Location{};
    double Distance{};
};

struct ObjectiveCandidate
{
    FVector Location{};
    std::wstring Name;
    double Distance{};
    bool Active{};
    bool Complete{};
};

struct InspectorActorEntry
{
    std::uintptr_t Address{};
    std::string ClassName;
    std::wstring FriendlyName;
    MarkerCategory Category{MarkerCategory::None};
    double Distance{};
    FVector Location{};
    bool Alive{};
};

struct SkeletonBoneSet
{
    std::array<FName, 16> Names{};
    std::array<bool, 16> Valid{};
};

struct ClassificationRoots
{
    UClass* Pickup{};
    UClass* FirearmPickup{};
    UClass* MeleePickup{};
    UClass* DeployablePickup{};
    UClass* ItemRack{};
    UClass* Container{};
    UClass* BlueprintContainer{};
    UClass* WorldObjective{};
    UClass* ExtractionPoint{};
    UClass* ExtractionVehicle{};
    UClass* Vehicle{};

    bool IsValid() const
    {
        return Pickup && FirearmPickup && MeleePickup && DeployablePickup &&
               ItemRack && Container && BlueprintContainer && WorldObjective &&
               ExtractionPoint && ExtractionVehicle && Vehicle;
    }
};

struct VTablePatch
{
    void** Slot{};
    void* Original{};
};

struct DamageAttributeValues
{
    float Base{};
    float Current{};
};

struct DamageModifierState
{
    UKAttributeSet_Global* AttributeSet{};
    std::array<DamageAttributeValues, 6> OriginalValues{};
    std::array<DamageAttributeValues, 6> AppliedValues{};
    float AppliedMultiplier{1.0F};
    bool IncludedStability{};
    bool AppliedOneHitMelee{};
    bool HasSnapshot{};
};

struct MeleeRangeState
{
    AKCharacter* Character{};
    AKItem* Item{};
    UKAttributeSet_Item* AttributeSet{};
    std::array<DamageAttributeValues, 3> OriginalValues{};
    std::array<DamageAttributeValues, 3> AppliedValues{};
    bool HasSnapshot{};
};

struct SpeedModifierState
{
    APawn* Pawn{};
    float OriginalTimeDilation{1.0F};
    float AppliedTimeDilation{1.0F};
    float AppliedMultiplier{1.0F};
    bool HasSnapshot{};
};

struct FullbrightState
{
    int32 EyeAdaptationQuality{};
    float ExposureOffset{};
    float TonemapperGamma{};
    float AppliedIntensity{};
    bool HasSnapshot{};
};

struct GhostModeState
{
    APlayerController* Controller{};
    UKCheatManager* Manager{};
    bool Applied{};
};

struct WeaponModifierState
{
    AKCharacter* Character{};
    AKItem* Item{};
    UKAttributeSet_Item* AttributeSet{};
    std::array<DamageAttributeValues, 21> OriginalValues{};
    std::array<DamageAttributeValues, 21> AppliedValues{};
    bool HasSnapshot{};
};

struct StaminaModifierState
{
    UKAttributeSet_Stamina* AttributeSet{};
    DamageAttributeValues OriginalValue{};
    DamageAttributeValues AppliedValue{};
    bool HasSnapshot{};
};

struct GodModeState
{
    APawn* Pawn{};
    bool OriginalCanBeDamaged{true};
    bool Applied{};
};

bool IsOfflineWorld(const UWorld* World);

struct GlobalTimeState
{
    UWorld* World{};
    float OriginalScale{1.0F};
    float AppliedScale{1.0F};
    bool HasSnapshot{};
};

struct PhotoModeState
{
    UWorld* World{};
    APlayerController* Controller{};
    UKCheatManager* Manager{};
    bool Applied{};
};

struct ZombieTimeState
{
    AKCharacter* Character{};
    float OriginalScale{1.0F};
    float AppliedScale{1.0F};
};

struct AmmoCheatState
{
    UWorld* World{};
    APlayerController* Controller{};
    UKCheatManager* Manager{};
    int AppliedMode{};
};

struct AimTargetCandidate
{
    AKCharacter* Character{};
    FVector AimLocation{};
    FVector2D ScreenLocation{};
    double ScreenDistanceSquared{std::numeric_limits<double>::max()};
    double WorldDistance{};
    bool Visible{};
};

std::atomic_bool g_Running{true};
std::atomic_bool g_OverlayEnabled{true};
std::atomic_uint32_t g_ActiveHookCalls{0};
std::atomic<ProcessEventFn> g_OriginalProcessEvent{nullptr};
std::vector<VTablePatch> g_Patches;
OverlaySettings g_Settings;
std::unordered_map<const UClass*, ClassDescriptor> g_ClassCache;
std::atomic_bool g_MenuOpen{true};
std::atomic_bool g_OfflineAllowed{false};
std::atomic_bool g_ImGuiReady{false};
std::atomic_bool g_ImGuiFailed{false};
std::atomic_bool g_UnloadRequested{false};
std::atomic_bool g_DamageModifierApplied{false};
std::atomic_bool g_MeleeRangeApplied{false};
std::atomic_bool g_SpeedModifierApplied{false};
std::atomic_bool g_FullbrightApplied{false};
std::atomic_bool g_GhostModeApplied{false};
std::atomic_bool g_WeaponModifierApplied{false};
std::atomic_bool g_StaminaModifierApplied{false};
std::atomic_bool g_GodModeApplied{false};
std::atomic_bool g_GlobalTimeApplied{false};
std::atomic_bool g_ZombieTimeApplied{false};
std::atomic_bool g_AmmoCheatApplied{false};
std::atomic_bool g_PhotoModeApplied{false};
std::atomic_bool g_AimTargetActive{false};
std::atomic_bool g_RequestTeleKillSingle{false};
std::atomic_bool g_RequestTeleKillAll{false};
std::atomic_bool g_RequestDestroyAllAi{false};
std::atomic_bool g_RequestSpawnItem{false};
std::atomic_bool g_RequestGiveAmmo{false};
std::atomic_bool g_RequestGiveSupplies{false};
std::atomic_bool g_RequestTeleportObjective{false};
std::atomic_bool g_RequestTeleportExtraction{false};
std::atomic_bool g_RequestTeleportPickup{false};
std::atomic_bool g_RequestPickupVacuum{false};
std::atomic<DamageModifierStatus> g_DamageModifierStatus{DamageModifierStatus::Disabled};
std::mutex g_SettingsMutex;
std::mutex g_DamageModifierMutex;
std::mutex g_InspectorMutex;
std::mutex g_DiagnosticConsoleMutex;
DamageModifierState g_DamageModifierState;
MeleeRangeState g_MeleeRangeState;
SpeedModifierState g_SpeedModifierState;
FullbrightState g_FullbrightState;
GhostModeState g_GhostModeState;
WeaponModifierState g_WeaponModifierState;
StaminaModifierState g_StaminaModifierState;
GodModeState g_GodModeState;
GlobalTimeState g_GlobalTimeState;
PhotoModeState g_PhotoModeState;
std::vector<ZombieTimeState> g_ZombieTimeStates;
AmmoCheatState g_AmmoCheatState;
std::unordered_map<const USkeletalMeshComponent*, SkeletonBoneSet> g_SkeletonCache;
std::vector<InspectorActorEntry> g_InspectorSnapshot;
std::deque<std::string> g_DiagnosticConsole;
std::filesystem::path g_ModuleDirectory;
UINT64 g_LastAutoHealTick{};
UINT64 g_LastFlashlightRechargeTick{};
HANDLE g_LogHandle{};
PVOID g_ExceptionHandler{};
std::atomic_flag g_LogBusy = ATOMIC_FLAG_INIT;
std::atomic<UINT64> g_ActorScanUnlockTick{0};
std::atomic_uint64_t g_OverlayFrameCounter{0};
std::atomic_uint64_t g_ImGuiFrameCounter{0};
std::atomic_bool g_WarmupLogged{false};
std::atomic_bool g_ActorScanUnlockedLogged{false};
std::atomic_bool g_FirstImGuiFrameLogged{false};
std::atomic_bool g_ImGuiInitFailureLogged{false};
std::atomic_bool g_HudPatchLogged{false};
std::atomic<DiagnosticStage> g_LastDiagnosticStage{DiagnosticStage::Idle};
std::atomic<DWORD> g_LastDiagnosticThread{0};
std::atomic_uint32_t g_ExceptionLogCount{0};

thread_local DiagnosticStage g_DiagnosticStage{DiagnosticStage::Idle};
thread_local int32 g_DiagnosticLevelIndex{-1};
thread_local int32 g_DiagnosticActorIndex{-1};
thread_local const void* g_DiagnosticObject{};
thread_local const void* g_DiagnosticClass{};

const wchar_t* CategoryLabel(MarkerCategory Category);
FLinearColor CategoryColor(MarkerCategory Category);
bool AnyGameplayModifierApplied();

struct ScopedDiagnosticStage
{
    explicit ScopedDiagnosticStage(DiagnosticStage Stage)
        : Previous(g_DiagnosticStage)
    {
        g_DiagnosticStage = Stage;
        g_LastDiagnosticStage.store(Stage, std::memory_order_relaxed);
        g_LastDiagnosticThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    }

    ~ScopedDiagnosticStage()
    {
        g_DiagnosticStage = Previous;
    }

    DiagnosticStage Previous;
};

void WriteDiagnosticLog(const char* Format, ...)
{
    if (!Format)
        return;
    if (g_LogBusy.test_and_set(std::memory_order_acquire))
        return;

    char Message[1536]{};
    va_list Arguments;
    va_start(Arguments, Format);
    const int MessageLength = std::vsnprintf(Message, sizeof(Message), Format, Arguments);
    va_end(Arguments);

    if (MessageLength >= 0)
    {
        char Line[1792]{};
        const int LineLength = std::snprintf(
            Line,
            sizeof(Line),
            "[%llu][thread %lu] %s\r\n",
            static_cast<unsigned long long>(GetTickCount64()),
            static_cast<unsigned long>(GetCurrentThreadId()),
            Message);
        if (LineLength > 0)
        {
            {
                std::scoped_lock ConsoleLock(g_DiagnosticConsoleMutex);
                g_DiagnosticConsole.emplace_back(Line);
                while (g_DiagnosticConsole.size() > 400)
                    g_DiagnosticConsole.pop_front();
            }
            if (g_LogHandle && g_LogHandle != INVALID_HANDLE_VALUE)
            {
                const DWORD BytesToWrite = static_cast<DWORD>(
                    std::min<std::size_t>(static_cast<std::size_t>(LineLength), sizeof(Line) - 1));
                DWORD BytesWritten{};
                WriteFile(g_LogHandle, Line, BytesToWrite, &BytesWritten, nullptr);
                FlushFileBuffers(g_LogHandle);
            }
        }
    }

    g_LogBusy.clear(std::memory_order_release);
}

bool TryOpenLogAtPath(const std::wstring& Path)
{
    if (Path.empty())
        return false;

    g_LogHandle = CreateFileW(
        Path.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    return g_LogHandle && g_LogHandle != INVALID_HANDLE_VALUE;
}

bool OpenDiagnosticLog(HMODULE Module)
{
    constexpr wchar_t LogName[] = L"NMRiH2_Offline_DebugOverlay.log";
    wchar_t ModulePath[MAX_PATH]{};
    const DWORD ModuleLength = GetModuleFileNameW(
        Module,
        ModulePath,
        static_cast<DWORD>(std::size(ModulePath)));
    if (ModuleLength > 0 && ModuleLength < std::size(ModulePath))
    {
        std::wstring Path(ModulePath, ModuleLength);
        const std::size_t Separator = Path.find_last_of(L"\\/");
        if (Separator != std::wstring::npos)
        {
            Path.resize(Separator + 1);
            g_ModuleDirectory = std::filesystem::path(Path);
        }
        else
            Path.clear();
        Path += LogName;
        if (TryOpenLogAtPath(Path))
            return true;
    }

    wchar_t TemporaryPath[MAX_PATH]{};
    const DWORD TemporaryLength = GetTempPathW(
        static_cast<DWORD>(std::size(TemporaryPath)),
        TemporaryPath);
    if (TemporaryLength == 0 || TemporaryLength >= std::size(TemporaryPath))
        return false;

    std::wstring FallbackPath(TemporaryPath, TemporaryLength);
    FallbackPath += LogName;
    return TryOpenLogAtPath(FallbackPath);
}

constexpr std::array<const char*, 6> kProfileNames{
    "Default", "Combat", "Loot Search", "Exploration", "Custom 1", "Custom 2"};

std::filesystem::path ProfilePath(int Index)
{
    Index = std::clamp(Index, 0, static_cast<int>(kProfileNames.size()) - 1);
    std::error_code Error;
    std::filesystem::path Root = g_ModuleDirectory;
    if (Root.empty())
        Root = std::filesystem::current_path(Error);
    Root /= "NMRiH2_Offline_DebugOverlay_profiles";
    std::filesystem::create_directories(Root, Error);
    std::string Name = kProfileNames[static_cast<std::size_t>(Index)];
    std::replace(Name.begin(), Name.end(), ' ', '_');
    return Root / (Name + ".json");
}

std::string EscapeJson(std::string_view Value)
{
    std::string Result;
    Result.reserve(Value.size());
    for (const char Character : Value)
    {
        if (Character == '\\' || Character == '"')
            Result.push_back('\\');
        if (Character == '\n')
        {
            Result += "\\n";
            continue;
        }
        Result.push_back(Character);
    }
    return Result;
}

bool SaveProfileJson(int Index, const OverlaySettings& Settings)
{
    std::ofstream File(ProfilePath(Index), std::ios::trunc);
    if (!File)
        return false;

    bool First = true;
    const auto Prefix = [&](const char* Name)
    {
        File << (First ? "" : ",") << "\n  \"" << Name << "\": ";
        First = false;
    };
    const auto Bool = [&](const char* Name, bool Value) { Prefix(Name); File << (Value ? "true" : "false"); };
    const auto Number = [&](const char* Name, double Value) { Prefix(Name); File << std::setprecision(8) << Value; };
    const auto Integer = [&](const char* Name, int Value) { Prefix(Name); File << Value; };
    const auto String = [&](const char* Name, const char* Value)
    {
        Prefix(Name);
        File << '"' << EscapeJson(Value ? Value : "") << '"';
    };
    const auto Color = [&](const char* Name, const float* Value)
    {
        Prefix(Name);
        File << '[' << Value[0] << ',' << Value[1] << ',' << Value[2] << ',' << Value[3] << ']';
    };

    File << '{';
    Integer("version", 2);
    Number("RadiusMeters", Settings.RadiusMeters); Integer("MaxMarkers", Settings.MaxMarkers);
    Bool("ShowPlayers", Settings.ShowPlayers); Bool("ShowZombies", Settings.ShowZombies);
    Bool("ShowLoot", Settings.ShowLoot); Bool("ShowFirearms", Settings.ShowFirearms);
    Bool("ShowMelee", Settings.ShowMelee); Bool("ShowAmmo", Settings.ShowAmmo);
    Bool("ShowMedical", Settings.ShowMedical); Bool("ShowExplosives", Settings.ShowExplosives);
    Bool("ShowAttachments", Settings.ShowAttachments); Bool("ShowEquipment", Settings.ShowEquipment);
    Bool("ShowSupplies", Settings.ShowSupplies); Bool("ShowDeployables", Settings.ShowDeployables);
    Bool("ShowObjectiveItems", Settings.ShowObjectiveItems); Bool("ShowMiscPickups", Settings.ShowMiscPickups);
    Bool("Ammo9mm", Settings.Ammo9mm); Bool("Ammo45Acp", Settings.Ammo45Acp);
    Bool("Ammo22Lr", Settings.Ammo22Lr); Bool("Ammo357", Settings.Ammo357);
    Bool("Ammo308", Settings.Ammo308); Bool("Ammo556", Settings.Ammo556);
    Bool("Ammo68x51", Settings.Ammo68x51); Bool("Ammo12Gauge", Settings.Ammo12Gauge);
    Bool("ShowWorld", Settings.ShowWorld); Bool("ShowContainers", Settings.ShowContainers);
    Bool("ShowObjectives", Settings.ShowObjectives); Bool("ShowExtractions", Settings.ShowExtractions);
    Bool("ShowInteractables", Settings.ShowInteractables); Bool("ShowDoors", Settings.ShowDoors);
    Bool("ShowVehicles", Settings.ShowVehicles); Bool("ShowDebugActors", Settings.ShowDebugActors);
    Bool("ShowHealth", Settings.ShowHealth); Bool("ShowHealthBars", Settings.ShowHealthBars);
    Bool("ShowDistance", Settings.ShowDistance); Bool("ShowNames", Settings.ShowNames);
    Bool("ShowClassNames", Settings.ShowClassNames); Bool("ShowStatus", Settings.ShowStatus);
    Bool("ShowLegend", Settings.ShowLegend); Bool("ShowSkeleton", Settings.ShowSkeleton);
    Bool("ShowSnaplines", Settings.ShowSnaplines); Bool("ShowHeadMarkers", Settings.ShowHeadMarkers);
    Bool("ShowZombieTypeLabels", Settings.ShowZombieTypeLabels); Bool("ShowRadar", Settings.ShowRadar);
    Bool("RadarShowZombies", Settings.RadarShowZombies); Bool("RadarShowLoot", Settings.RadarShowLoot);
    Bool("RadarShowObjectives", Settings.RadarShowObjectives); Number("RadarRangeMeters", Settings.RadarRangeMeters);
    Number("RadarSize", Settings.RadarSize); Bool("ShowThreatIndicators", Settings.ShowThreatIndicators);
    Number("ThreatRadiusMeters", Settings.ThreatRadiusMeters); Bool("ShowWeaponInfo", Settings.ShowWeaponInfo);
    Bool("ShowObjectiveTracker", Settings.ShowObjectiveTracker); Bool("ShowDiagnosticOverlay", Settings.ShowDiagnosticOverlay);
    Integer("DiagnosticOverlayLines", Settings.DiagnosticOverlayLines); Bool("EnableActorInspector", Settings.EnableActorInspector);
    Bool("InspectorIncludeAllActors", Settings.InspectorIncludeAllActors); Integer("InspectorMaxEntries", Settings.InspectorMaxEntries);
    Bool("PhotoMode", Settings.PhotoMode); Number("PhotoTimeScale", Settings.PhotoTimeScale);
    Bool("EnableZombieAimbot", Settings.EnableZombieAimbot); Bool("DrawAimFov", Settings.DrawAimFov);
    Bool("AimVisibilityCheck", Settings.AimVisibilityCheck); Bool("AimHipfireMode", Settings.AimHipfireMode);
    Bool("EnableSimpleRcs", Settings.EnableSimpleRcs); Number("AimFovPixels", Settings.AimFovPixels);
    Number("AimSpeed", Settings.AimSpeed); Number("RcsStrength", Settings.RcsStrength);
    Bool("EnableOfflineDamageModifier", Settings.EnableOfflineDamageModifier); Number("OfflineDamageMultiplier", Settings.OfflineDamageMultiplier);
    Bool("IncludeStabilityDamage", Settings.IncludeStabilityDamage); Bool("OneHitMelee", Settings.OneHitMelee);
    Bool("DoubleMeleeRange", Settings.DoubleMeleeRange); Bool("Fullbright", Settings.Fullbright);
    Number("FullbrightIntensity", Settings.FullbrightIntensity); Bool("GhostMode", Settings.GhostMode);
    Bool("SpeedHack", Settings.SpeedHack); Number("SpeedMultiplier", Settings.SpeedMultiplier);
    Integer("AmmoCheatMode", Settings.AmmoCheatMode); Bool("InfiniteStamina", Settings.InfiniteStamina);
    Bool("NoRecoil", Settings.NoRecoil); Bool("ModifyWeaponSpread", Settings.ModifyWeaponSpread);
    Number("WeaponSpreadMultiplier", Settings.WeaponSpreadMultiplier); Bool("ModifyReloadSpeed", Settings.ModifyReloadSpeed);
    Number("ReloadSpeedMultiplier", Settings.ReloadSpeedMultiplier); Bool("ModifyFireRate", Settings.ModifyFireRate);
    Number("FireRateMultiplier", Settings.FireRateMultiplier); Bool("GodMode", Settings.GodMode);
    Bool("AutoHealthRegeneration", Settings.AutoHealthRegeneration); Bool("InfiniteFlashlightBattery", Settings.InfiniteFlashlightBattery);
    Bool("ModifyZombieTime", Settings.ModifyZombieTime); Number("ZombieTimeScale", Settings.ZombieTimeScale);
    Bool("ModifyGlobalTime", Settings.ModifyGlobalTime); Number("GlobalTimeScale", Settings.GlobalTimeScale);
    Integer("GiveAmmoAmount", Settings.GiveAmmoAmount); Integer("GiveSuppliesAmount", Settings.GiveSuppliesAmount);
    Number("PickupVacuumRadiusMeters", Settings.PickupVacuumRadiusMeters);
    String("AmmoGrantIdentifier", Settings.AmmoGrantIdentifier); String("SpawnItemIdentifier", Settings.SpawnItemIdentifier);
    Number("MarkerSize", Settings.MarkerSize); Number("TextScale", Settings.TextScale); Number("BoxThickness", Settings.BoxThickness);
    Color("MenuAccent", Settings.MenuAccent); Color("MenuBackground", Settings.MenuBackground);
    Color("MenuPanel", Settings.MenuPanel); Color("MenuText", Settings.MenuText);
    Color("PlayerEspColor", Settings.PlayerEspColor); Color("ZombieEspColor", Settings.ZombieEspColor);
    Color("SkeletonColor", Settings.SkeletonColor); Color("HeadMarkerColor", Settings.HeadMarkerColor);
    Color("SnaplineColor", Settings.SnaplineColor); Color("ThreatColor", Settings.ThreatColor);
    Bool("RainbowAccent", Settings.RainbowAccent); Number("RainbowSpeed", Settings.RainbowSpeed);
    Integer("MenuHotkey", Settings.MenuHotkey); Integer("OverlayHotkey", Settings.OverlayHotkey);
    Integer("NextPageHotkey", Settings.NextPageHotkey); Integer("UnloadHotkey", Settings.UnloadHotkey);
    Integer("TeleKillSingleHotkey", Settings.TeleKillSingleHotkey); Integer("TeleKillAllHotkey", Settings.TeleKillAllHotkey);
    for (std::size_t ColorIndex = 0; ColorIndex < Settings.CategoryColors.size(); ++ColorIndex)
    {
        const std::string Name = "CategoryColor" + std::to_string(ColorIndex);
        Color(Name.c_str(), Settings.CategoryColors[ColorIndex].data());
    }
    File << "\n}\n";
    File.flush();
    const bool Success = File.good();
    WriteDiagnosticLog("Profile %s: %s", Success ? "saved" : "save failed", ProfilePath(Index).string().c_str());
    return Success;
}

std::size_t FindJsonValue(const std::string& Json, const char* Name)
{
    const std::string Needle = std::string("\"") + Name + "\"";
    const std::size_t Key = Json.find(Needle);
    if (Key == std::string::npos)
        return Key;
    const std::size_t Colon = Json.find(':', Key + Needle.size());
    if (Colon == std::string::npos)
        return Colon;
    return Json.find_first_not_of(" \t\r\n", Colon + 1);
}

bool ReadJsonBool(const std::string& Json, const char* Name, bool* Value)
{
    const std::size_t Position = FindJsonValue(Json, Name);
    if (Position == std::string::npos || !Value)
        return false;
    if (Json.compare(Position, 4, "true") == 0) { *Value = true; return true; }
    if (Json.compare(Position, 5, "false") == 0) { *Value = false; return true; }
    return false;
}

template <typename T>
bool ReadJsonNumber(const std::string& Json, const char* Name, T* Value)
{
    const std::size_t Position = FindJsonValue(Json, Name);
    if (Position == std::string::npos || !Value)
        return false;
    char* End{};
    const double Parsed = std::strtod(Json.c_str() + Position, &End);
    if (End == Json.c_str() + Position || !std::isfinite(Parsed))
        return false;
    *Value = static_cast<T>(Parsed);
    return true;
}

bool ReadJsonString(const std::string& Json, const char* Name, char* Value, std::size_t Capacity)
{
    const std::size_t Position = FindJsonValue(Json, Name);
    if (Position == std::string::npos || !Value || Capacity == 0 || Json[Position] != '"')
        return false;
    std::string Parsed;
    bool Escaped = false;
    for (std::size_t Index = Position + 1; Index < Json.size(); ++Index)
    {
        const char Character = Json[Index];
        if (!Escaped && Character == '"')
            break;
        if (!Escaped && Character == '\\') { Escaped = true; continue; }
        Parsed.push_back(Escaped && Character == 'n' ? '\n' : Character);
        Escaped = false;
    }
    std::snprintf(Value, Capacity, "%s", Parsed.c_str());
    return true;
}

bool ReadJsonColor(const std::string& Json, const char* Name, float* Value)
{
    const std::size_t Position = FindJsonValue(Json, Name);
    if (Position == std::string::npos || !Value || Json[Position] != '[')
        return false;
    std::size_t Cursor = Position + 1;
    for (int Channel = 0; Channel < 4; ++Channel)
    {
        Cursor = Json.find_first_not_of(" \t\r\n,", Cursor);
        if (Cursor == std::string::npos)
            return false;
        char* End{};
        const double Parsed = std::strtod(Json.c_str() + Cursor, &End);
        if (End == Json.c_str() + Cursor || !std::isfinite(Parsed))
            return false;
        Value[Channel] = std::clamp(static_cast<float>(Parsed), 0.0F, 1.0F);
        Cursor = static_cast<std::size_t>(End - Json.c_str());
    }
    return true;
}

bool LoadProfileJson(int Index, OverlaySettings* Settings)
{
    if (!Settings)
        return false;
    std::ifstream File(ProfilePath(Index));
    if (!File)
        return false;
    const std::string Json((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
    if (Json.empty())
        return false;
    OverlaySettings Loaded = *Settings;
#define LOAD_BOOL(Field) ReadJsonBool(Json, #Field, &Loaded.Field)
#define LOAD_NUM(Field) ReadJsonNumber(Json, #Field, &Loaded.Field)
    LOAD_NUM(RadiusMeters); LOAD_NUM(MaxMarkers); LOAD_BOOL(ShowPlayers); LOAD_BOOL(ShowZombies);
    LOAD_BOOL(ShowLoot); LOAD_BOOL(ShowFirearms); LOAD_BOOL(ShowMelee); LOAD_BOOL(ShowAmmo);
    LOAD_BOOL(ShowMedical); LOAD_BOOL(ShowExplosives); LOAD_BOOL(ShowAttachments); LOAD_BOOL(ShowEquipment);
    LOAD_BOOL(ShowSupplies); LOAD_BOOL(ShowDeployables); LOAD_BOOL(ShowObjectiveItems); LOAD_BOOL(ShowMiscPickups);
    LOAD_BOOL(Ammo9mm); LOAD_BOOL(Ammo45Acp); LOAD_BOOL(Ammo22Lr); LOAD_BOOL(Ammo357);
    LOAD_BOOL(Ammo308); LOAD_BOOL(Ammo556); LOAD_BOOL(Ammo68x51); LOAD_BOOL(Ammo12Gauge);
    LOAD_BOOL(ShowWorld); LOAD_BOOL(ShowContainers); LOAD_BOOL(ShowObjectives); LOAD_BOOL(ShowExtractions);
    LOAD_BOOL(ShowInteractables); LOAD_BOOL(ShowDoors); LOAD_BOOL(ShowVehicles); LOAD_BOOL(ShowDebugActors);
    LOAD_BOOL(ShowHealth); LOAD_BOOL(ShowHealthBars); LOAD_BOOL(ShowDistance); LOAD_BOOL(ShowNames);
    LOAD_BOOL(ShowClassNames); LOAD_BOOL(ShowStatus); LOAD_BOOL(ShowLegend); LOAD_BOOL(ShowSkeleton);
    LOAD_BOOL(ShowSnaplines); LOAD_BOOL(ShowHeadMarkers); LOAD_BOOL(ShowZombieTypeLabels); LOAD_BOOL(ShowRadar);
    LOAD_BOOL(RadarShowZombies); LOAD_BOOL(RadarShowLoot); LOAD_BOOL(RadarShowObjectives); LOAD_NUM(RadarRangeMeters);
    LOAD_NUM(RadarSize); LOAD_BOOL(ShowThreatIndicators); LOAD_NUM(ThreatRadiusMeters); LOAD_BOOL(ShowWeaponInfo);
    LOAD_BOOL(ShowObjectiveTracker); LOAD_BOOL(ShowDiagnosticOverlay); LOAD_NUM(DiagnosticOverlayLines);
    LOAD_BOOL(EnableActorInspector); LOAD_BOOL(InspectorIncludeAllActors); LOAD_NUM(InspectorMaxEntries);
    LOAD_BOOL(PhotoMode); LOAD_NUM(PhotoTimeScale); LOAD_BOOL(EnableZombieAimbot); LOAD_BOOL(DrawAimFov);
    LOAD_BOOL(AimVisibilityCheck); LOAD_BOOL(AimHipfireMode); LOAD_BOOL(EnableSimpleRcs); LOAD_NUM(AimFovPixels);
    LOAD_NUM(AimSpeed); LOAD_NUM(RcsStrength); LOAD_BOOL(EnableOfflineDamageModifier); LOAD_NUM(OfflineDamageMultiplier);
    LOAD_BOOL(IncludeStabilityDamage); LOAD_BOOL(OneHitMelee); LOAD_BOOL(DoubleMeleeRange); LOAD_BOOL(Fullbright);
    LOAD_NUM(FullbrightIntensity); LOAD_BOOL(GhostMode); LOAD_BOOL(SpeedHack); LOAD_NUM(SpeedMultiplier);
    LOAD_NUM(AmmoCheatMode); LOAD_BOOL(InfiniteStamina); LOAD_BOOL(NoRecoil); LOAD_BOOL(ModifyWeaponSpread);
    LOAD_NUM(WeaponSpreadMultiplier); LOAD_BOOL(ModifyReloadSpeed); LOAD_NUM(ReloadSpeedMultiplier);
    LOAD_BOOL(ModifyFireRate); LOAD_NUM(FireRateMultiplier); LOAD_BOOL(GodMode); LOAD_BOOL(AutoHealthRegeneration);
    LOAD_BOOL(InfiniteFlashlightBattery); LOAD_BOOL(ModifyZombieTime); LOAD_NUM(ZombieTimeScale);
    LOAD_BOOL(ModifyGlobalTime); LOAD_NUM(GlobalTimeScale); LOAD_NUM(GiveAmmoAmount); LOAD_NUM(GiveSuppliesAmount);
    LOAD_NUM(PickupVacuumRadiusMeters);
    LOAD_NUM(MarkerSize); LOAD_NUM(TextScale); LOAD_NUM(BoxThickness); LOAD_BOOL(RainbowAccent); LOAD_NUM(RainbowSpeed);
    LOAD_NUM(MenuHotkey); LOAD_NUM(OverlayHotkey); LOAD_NUM(NextPageHotkey); LOAD_NUM(UnloadHotkey);
    LOAD_NUM(TeleKillSingleHotkey); LOAD_NUM(TeleKillAllHotkey);
#undef LOAD_BOOL
#undef LOAD_NUM
    ReadJsonString(Json, "AmmoGrantIdentifier", Loaded.AmmoGrantIdentifier, sizeof(Loaded.AmmoGrantIdentifier));
    ReadJsonString(Json, "SpawnItemIdentifier", Loaded.SpawnItemIdentifier, sizeof(Loaded.SpawnItemIdentifier));
    ReadJsonColor(Json, "MenuAccent", Loaded.MenuAccent); ReadJsonColor(Json, "MenuBackground", Loaded.MenuBackground);
    ReadJsonColor(Json, "MenuPanel", Loaded.MenuPanel); ReadJsonColor(Json, "MenuText", Loaded.MenuText);
    ReadJsonColor(Json, "PlayerEspColor", Loaded.PlayerEspColor); ReadJsonColor(Json, "ZombieEspColor", Loaded.ZombieEspColor);
    ReadJsonColor(Json, "SkeletonColor", Loaded.SkeletonColor); ReadJsonColor(Json, "HeadMarkerColor", Loaded.HeadMarkerColor);
    ReadJsonColor(Json, "SnaplineColor", Loaded.SnaplineColor); ReadJsonColor(Json, "ThreatColor", Loaded.ThreatColor);
    for (std::size_t ColorIndex = 0; ColorIndex < Loaded.CategoryColors.size(); ++ColorIndex)
    {
        const std::string Name = "CategoryColor" + std::to_string(ColorIndex);
        ReadJsonColor(Json, Name.c_str(), Loaded.CategoryColors[ColorIndex].data());
    }
    Loaded.ActiveTab = Settings->ActiveTab;
    Loaded.SelectedProfile = Index;
    Loaded.SelectedMenuRow = 0;
    *Settings = Loaded;
    WriteDiagnosticLog("Profile loaded: %s", ProfilePath(Index).string().c_str());
    return true;
}

void ApplyVisualPreset(OverlaySettings& Settings, int Preset)
{
    Settings.ShowPlayers = false;
    Settings.ShowZombies = true;
    Settings.ShowHealth = true;
    Settings.ShowDistance = true;
    Settings.ShowRadar = true;
    Settings.ShowThreatIndicators = true;
    Settings.ShowObjectiveTracker = true;
    if (Preset == 0)
    {
        Settings.ShowSkeleton = true; Settings.ShowHeadMarkers = true; Settings.ShowSnaplines = false;
        Settings.RadarShowZombies = true; Settings.RadarShowLoot = false; Settings.RadarShowObjectives = true;
        Settings.ShowLoot = false; Settings.ShowWorld = true; Settings.RadiusMeters = 650.0;
    }
    else if (Preset == 1)
    {
        Settings.ShowSkeleton = false; Settings.ShowHeadMarkers = true; Settings.ShowSnaplines = false;
        Settings.RadarShowZombies = true; Settings.RadarShowLoot = true; Settings.RadarShowObjectives = true;
        Settings.ShowLoot = true; Settings.ShowWorld = true; Settings.RadiusMeters = 1500.0;
    }
    else
    {
        Settings.ShowSkeleton = false; Settings.ShowHeadMarkers = false; Settings.ShowSnaplines = false;
        Settings.RadarShowZombies = true; Settings.RadarShowLoot = false; Settings.RadarShowObjectives = true;
        Settings.ShowLoot = false; Settings.ShowWorld = true; Settings.ShowDoors = true; Settings.RadiusMeters = 2500.0;
    }
    WriteDiagnosticLog("Visual preset applied: %s", Preset == 0 ? "Combat" : (Preset == 1 ? "Loot Search" : "Exploration"));
}

LONG CALLBACK OverlayExceptionHandler(EXCEPTION_POINTERS* Information)
{
    if (!Information || !Information->ExceptionRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    const EXCEPTION_RECORD* Record = Information->ExceptionRecord;
    constexpr DWORD StatusHeapCorruption = 0xC0000374U;
    constexpr DWORD StatusStackBufferOverrun = 0xC0000409U;
    switch (Record->ExceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case StatusHeapCorruption:
    case StatusStackBufferOverrun:
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Unreal and graphics drivers can raise the final fault on a worker thread
    // that never entered our hook. Keep a small first-chance budget so those
    // failures are visible without turning handled game exceptions into an
    // unbounded log stream.
    if (g_ExceptionLogCount.fetch_add(1, std::memory_order_relaxed) >= 32)
        return EXCEPTION_CONTINUE_SEARCH;

    const DiagnosticStage LastStage = g_LastDiagnosticStage.load(std::memory_order_relaxed);
    const DWORD LastStageThread = g_LastDiagnosticThread.load(std::memory_order_relaxed);

    if ((Record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
         Record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) &&
        Record->NumberParameters >= 2)
    {
        WriteDiagnosticLog(
            "EXCEPTION code=0x%08lX address=%p access=%llu target=%p stage=%s "
            "level=%d actor=%d object=%p class=%p last_overlay_stage=%s "
            "last_overlay_thread=%lu",
            static_cast<unsigned long>(Record->ExceptionCode),
            Record->ExceptionAddress,
            static_cast<unsigned long long>(Record->ExceptionInformation[0]),
            reinterpret_cast<const void*>(Record->ExceptionInformation[1]),
            DiagnosticStageName(g_DiagnosticStage),
            g_DiagnosticLevelIndex,
            g_DiagnosticActorIndex,
            g_DiagnosticObject,
            g_DiagnosticClass,
            DiagnosticStageName(LastStage),
            static_cast<unsigned long>(LastStageThread));
    }
    else
    {
        WriteDiagnosticLog(
            "EXCEPTION code=0x%08lX address=%p stage=%s level=%d actor=%d "
            "object=%p class=%p last_overlay_stage=%s last_overlay_thread=%lu",
            static_cast<unsigned long>(Record->ExceptionCode),
            Record->ExceptionAddress,
            DiagnosticStageName(g_DiagnosticStage),
            g_DiagnosticLevelIndex,
            g_DiagnosticActorIndex,
            g_DiagnosticObject,
            g_DiagnosticClass,
            DiagnosticStageName(LastStage),
            static_cast<unsigned long>(LastStageThread));
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

#ifndef OVERLAY_SOURCE_CHECK
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(
    IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(STDMETHODCALLTYPE*)(
    ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

struct Dx12FrameContext
{
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> Allocator;
    Microsoft::WRL::ComPtr<ID3D12Resource> BackBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE RenderTarget{};
    UINT64 FenceValue{};
};

struct ShaderDescriptorAllocator
{
    ID3D12DescriptorHeap* Heap{};
    UINT Increment{};
    D3D12_CPU_DESCRIPTOR_HANDLE CpuStart{};
    D3D12_GPU_DESCRIPTOR_HANDLE GpuStart{};
    std::vector<UINT> FreeIndices;
};

std::atomic_bool g_DxHooksInstalled{false};
std::atomic_uint32_t g_DirectQueueChangeCount{0};
std::mutex g_ImGuiMutex;
std::mutex g_QueueMutex;
thread_local ID3D12CommandQueue* g_ThreadDirectQueue{};
thread_local bool g_IsImGuiSubmission{};

PresentFn g_OriginalPresent{};
ResizeBuffersFn g_OriginalResizeBuffers{};
ExecuteCommandListsFn g_OriginalExecuteCommandLists{};
void* g_PresentTarget{};
void* g_ResizeBuffersTarget{};
void* g_ExecuteCommandListsTarget{};

Microsoft::WRL::ComPtr<ID3D12CommandQueue> g_LatestDirectQueue;
Microsoft::WRL::ComPtr<ID3D12CommandQueue> g_FirstDirectQueue;
Microsoft::WRL::ComPtr<ID3D12CommandQueue> g_RenderQueue;
Microsoft::WRL::ComPtr<ID3D12Device> g_ImGuiDevice;
Microsoft::WRL::ComPtr<IDXGISwapChain3> g_ImGuiSwapChain;
Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> g_ImGuiCommandList;
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_RenderTargetHeap;
Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> g_ShaderDescriptorHeap;
Microsoft::WRL::ComPtr<ID3D12Fence> g_ImGuiFence;
std::vector<Dx12FrameContext> g_Dx12Frames;
ShaderDescriptorAllocator g_ShaderDescriptors;
HANDLE g_ImGuiFenceEvent{};
UINT64 g_LastFenceValue{};
HWND g_GameWindow{};
WNDPROC g_OriginalWindowProc{};
bool g_Win32BackendReady{};
bool g_Dx12BackendReady{};

HANDLE g_ExternalUiThread{};
HWND g_TargetGameWindow{};
HWND g_ExternalOverlayWindow{};
Microsoft::WRL::ComPtr<ID3D11Device> g_ExternalDevice;
Microsoft::WRL::ComPtr<ID3D11DeviceContext> g_ExternalDeviceContext;
Microsoft::WRL::ComPtr<IDXGISwapChain> g_ExternalSwapChain;
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> g_ExternalRenderTarget;
UINT g_ExternalWidth{};
UINT g_ExternalHeight{};
LONG g_ExternalLeft{};
LONG g_ExternalTop{};
bool g_ExternalBoundsReady{};
#endif

#if !defined(OVERLAY_SOURCE_CHECK) || defined(OVERLAY_UI_SOURCE_CHECK)
ImFont* g_TitleFont{};
bool g_MenuPlacementInitialized{};
#endif

struct ActiveHookCall
{
    ActiveHookCall()
    {
        g_ActiveHookCalls.fetch_add(1, std::memory_order_acq_rel);
    }

    ~ActiveHookCall()
    {
        g_ActiveHookCalls.fetch_sub(1, std::memory_order_acq_rel);
    }
};

const char* DamageModifierStatusText()
{
    switch (g_DamageModifierStatus.load(std::memory_order_relaxed))
    {
    case DamageModifierStatus::Disabled: return "DISABLED";
    case DamageModifierStatus::WaitingForPlayer: return "WAITING FOR LOCAL PLAYER";
    case DamageModifierStatus::WaitingForAttributes: return "WAITING FOR DAMAGE ATTRIBUTES";
    case DamageModifierStatus::Applied: return "APPLIED - OFFLINE WORLD";
    case DamageModifierStatus::BlockedOnline: return "LOCKED - NETWORK DRIVER DETECTED";
    default: return "UNKNOWN";
    }
}

#if !defined(OVERLAY_SOURCE_CHECK) || defined(OVERLAY_UI_SOURCE_CHECK)
void ApplyImGuiStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& Style = ImGui::GetStyle();
    Style.WindowPadding = ImVec2(22.0F, 18.0F);
    Style.FramePadding = ImVec2(10.0F, 7.0F);
    Style.CellPadding = ImVec2(10.0F, 7.0F);
    Style.ItemSpacing = ImVec2(10.0F, 9.0F);
    Style.ItemInnerSpacing = ImVec2(8.0F, 6.0F);
    Style.ScrollbarSize = 12.0F;
    Style.WindowRounding = 17.0F;
    Style.ChildRounding = 11.0F;
    Style.FrameRounding = 7.0F;
    Style.PopupRounding = 9.0F;
    Style.ScrollbarRounding = 9.0F;
    Style.GrabRounding = 7.0F;
    Style.TabRounding = 7.0F;
    Style.WindowBorderSize = 1.0F;
    Style.ChildBorderSize = 1.0F;
    Style.FrameBorderSize = 0.0F;

    ImVec4* Colors = Style.Colors;
    Colors[ImGuiCol_Text] = ImVec4(0.91F, 0.95F, 1.00F, 1.00F);
    Colors[ImGuiCol_TextDisabled] = ImVec4(0.48F, 0.56F, 0.67F, 1.00F);
    Colors[ImGuiCol_WindowBg] = ImVec4(0.025F, 0.035F, 0.060F, 0.985F);
    Colors[ImGuiCol_ChildBg] = ImVec4(0.035F, 0.050F, 0.082F, 0.92F);
    Colors[ImGuiCol_PopupBg] = ImVec4(0.025F, 0.040F, 0.070F, 0.98F);
    Colors[ImGuiCol_Border] = ImVec4(0.16F, 0.30F, 0.47F, 0.78F);
    Colors[ImGuiCol_FrameBg] = ImVec4(0.070F, 0.105F, 0.165F, 1.00F);
    Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.09F, 0.26F, 0.38F, 1.00F);
    Colors[ImGuiCol_FrameBgActive] = ImVec4(0.08F, 0.36F, 0.52F, 1.00F);
    Colors[ImGuiCol_TitleBg] = ImVec4(0.025F, 0.035F, 0.060F, 1.00F);
    Colors[ImGuiCol_TitleBgActive] = ImVec4(0.025F, 0.035F, 0.060F, 1.00F);
    Colors[ImGuiCol_CheckMark] = ImVec4(0.24F, 0.88F, 1.00F, 1.00F);
    Colors[ImGuiCol_SliderGrab] = ImVec4(0.20F, 0.72F, 1.00F, 1.00F);
    Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.55F, 0.42F, 1.00F, 1.00F);
    Colors[ImGuiCol_Button] = ImVec4(0.07F, 0.15F, 0.24F, 1.00F);
    Colors[ImGuiCol_ButtonHovered] = ImVec4(0.08F, 0.34F, 0.50F, 1.00F);
    Colors[ImGuiCol_ButtonActive] = ImVec4(0.17F, 0.57F, 0.73F, 1.00F);
    Colors[ImGuiCol_Header] = ImVec4(0.07F, 0.25F, 0.37F, 1.00F);
    Colors[ImGuiCol_HeaderHovered] = ImVec4(0.09F, 0.36F, 0.50F, 1.00F);
    Colors[ImGuiCol_HeaderActive] = ImVec4(0.14F, 0.52F, 0.68F, 1.00F);
    Colors[ImGuiCol_Separator] = ImVec4(0.12F, 0.26F, 0.40F, 0.82F);
    Colors[ImGuiCol_ResizeGrip] = ImVec4(0.20F, 0.68F, 0.92F, 0.42F);
    Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.28F, 0.86F, 1.00F, 0.88F);
    Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.58F, 0.43F, 1.00F, 1.00F);
    Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02F, 0.03F, 0.05F, 0.55F);
    Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.12F, 0.31F, 0.46F, 0.88F);
}

ImVec4 MenuColor(const float (&Color)[4])
{
    return ImVec4(Color[0], Color[1], Color[2], Color[3]);
}

ImVec4 ScaledColor(const ImVec4& Color, float Scale, float Alpha = -1.0F)
{
    return ImVec4(
        std::clamp(Color.x * Scale, 0.0F, 1.0F),
        std::clamp(Color.y * Scale, 0.0F, 1.0F),
        std::clamp(Color.z * Scale, 0.0F, 1.0F),
        Alpha >= 0.0F ? Alpha : Color.w);
}

ImVec4 CurrentMenuAccent()
{
    if (!g_Settings.RainbowAccent)
        return MenuColor(g_Settings.MenuAccent);

    const float Speed = std::clamp(g_Settings.RainbowSpeed, 0.05F, 1.0F);
    const float Hue = std::fmod(static_cast<float>(ImGui::GetTime()) * Speed, 1.0F);
    ImVec4 Result{};
    ImGui::ColorConvertHSVtoRGB(Hue, 0.72F, 1.0F, Result.x, Result.y, Result.z);
    Result.w = 1.0F;
    return Result;
}

void ApplyDynamicMenuColors()
{
    ImGuiStyle& Style = ImGui::GetStyle();
    ImVec4* Colors = Style.Colors;
    const ImVec4 Accent = CurrentMenuAccent();
    const ImVec4 Background = MenuColor(g_Settings.MenuBackground);
    const ImVec4 Panel = MenuColor(g_Settings.MenuPanel);
    Colors[ImGuiCol_Text] = MenuColor(g_Settings.MenuText);
    Colors[ImGuiCol_WindowBg] = Background;
    Colors[ImGuiCol_ChildBg] = Panel;
    Colors[ImGuiCol_PopupBg] = ScaledColor(Background, 1.10F, 0.985F);
    Colors[ImGuiCol_Border] = ScaledColor(Accent, 0.55F, 0.78F);
    Colors[ImGuiCol_CheckMark] = Accent;
    Colors[ImGuiCol_SliderGrab] = ScaledColor(Accent, 0.88F);
    Colors[ImGuiCol_SliderGrabActive] = Accent;
    Colors[ImGuiCol_ButtonHovered] = ScaledColor(Accent, 0.56F);
    Colors[ImGuiCol_ButtonActive] = ScaledColor(Accent, 0.75F);
    Colors[ImGuiCol_Header] = ScaledColor(Accent, 0.38F);
    Colors[ImGuiCol_HeaderHovered] = ScaledColor(Accent, 0.55F);
    Colors[ImGuiCol_HeaderActive] = ScaledColor(Accent, 0.72F);
    Colors[ImGuiCol_Separator] = ScaledColor(Accent, 0.48F, 0.82F);
    Colors[ImGuiCol_ResizeGrip] = ScaledColor(Accent, 0.75F, 0.42F);
    Colors[ImGuiCol_ResizeGripHovered] = ScaledColor(Accent, 0.95F, 0.88F);
    Colors[ImGuiCol_ResizeGripActive] = Accent;
}

void BeginSettingsCard(const char* Id, const char* Title, float Height)
{
    ImGui::BeginChild(Id, ImVec2(0.0F, Height), ImGuiChildFlags_Borders);
    ImGui::TextColored(CurrentMenuAccent(), "%s", Title);
    ImGui::Separator();
    ImGui::Spacing();
}

void EndSettingsCard()
{
    ImGui::EndChild();
}

void RenderGeneralSettings()
{
    if (!ImGui::BeginTable("##GeneralColumns", 2, ImGuiTableFlags_SizingStretchSame))
        return;

    ImGui::TableNextColumn();
    BeginSettingsCard("##DetectionCard", "DETECTION", 182.0F);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderScalar(
        "Scan radius",
        ImGuiDataType_Double,
        &g_Settings.RadiusMeters,
        &kMinRadiusMeters,
        &kMaxRadiusMeters,
        "%.0f m",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderInt("Marker budget", &g_Settings.MaxMarkers, 25, 500, "%d actors");
    ImGui::Checkbox("Players", &g_Settings.ShowPlayers);
    ImGui::SameLine(150.0F);
    ImGui::Checkbox("Zombies", &g_Settings.ShowZombies);
    EndSettingsCard();

    BeginSettingsCard("##HudCard", "HUD ELEMENTS", 147.0F);
    ImGui::Checkbox("Status line", &g_Settings.ShowStatus);
    ImGui::Checkbox("Color legend", &g_Settings.ShowLegend);
    ImGui::Checkbox("Friendly names", &g_Settings.ShowNames);
    ImGui::Checkbox("Raw SDK class names", &g_Settings.ShowClassNames);
    EndSettingsCard();

    ImGui::TableNextColumn();
    BeginSettingsCard("##VisualCard", "MARKER STYLE", 182.0F);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Marker size", &g_Settings.MarkerSize, 3.0F, 14.0F, "%.0f px");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Text scale", &g_Settings.TextScale, 0.5F, 1.5F, "%.1f x");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Box thickness", &g_Settings.BoxThickness, 0.5F, 4.0F, "%.1f px");
    EndSettingsCard();

    BeginSettingsCard("##LabelsCard", "ENTITY DETAILS", 147.0F);
    ImGui::Checkbox("Health values", &g_Settings.ShowHealth);
    ImGui::Checkbox("Health bars", &g_Settings.ShowHealthBars);
    ImGui::Checkbox("Distance values", &g_Settings.ShowDistance);
    ImGui::TextDisabled("All rendering stays inside the offline radius.");
    EndSettingsCard();

    ImGui::EndTable();

    ImGui::Spacing();
    if (ImGui::BeginTable("##EnhancedHudColumns", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        BeginSettingsCard("##CharacterEspCard", "CHARACTER ESP", 225.0F);
        ImGui::Checkbox("Skeleton ESP", &g_Settings.ShowSkeleton);
        ImGui::Checkbox("Snaplines", &g_Settings.ShowSnaplines);
        ImGui::Checkbox("Head markers", &g_Settings.ShowHeadMarkers);
        ImGui::Checkbox("Zombie type labels", &g_Settings.ShowZombieTypeLabels);
        ImGui::Checkbox("Weapon information panel", &g_Settings.ShowWeaponInfo);
        ImGui::Checkbox("Objective tracker", &g_Settings.ShowObjectiveTracker);
        ImGui::TextDisabled("Skeleton sockets are resolved from each live mesh.");
        EndSettingsCard();

        ImGui::TableNextColumn();
        BeginSettingsCard("##RadarThreatCard", "RADAR / THREATS", 225.0F);
        ImGui::Checkbox("Radar / minimap", &g_Settings.ShowRadar);
        ImGui::Checkbox("Radar: zombies", &g_Settings.RadarShowZombies);
        ImGui::SameLine(); ImGui::Checkbox("loot", &g_Settings.RadarShowLoot);
        ImGui::SameLine(); ImGui::Checkbox("objectives", &g_Settings.RadarShowObjectives);
        ImGui::SliderFloat("Radar range", &g_Settings.RadarRangeMeters, 25.0F, 1000.0F, "%.0f m");
        ImGui::SliderFloat("Radar size", &g_Settings.RadarSize, 120.0F, 320.0F, "%.0f px");
        ImGui::Checkbox("Incoming-hostile indicators", &g_Settings.ShowThreatIndicators);
        ImGui::SliderFloat("Threat radius", &g_Settings.ThreatRadiusMeters, 10.0F, 150.0F, "%.0f m");
        EndSettingsCard();
        ImGui::EndTable();
    }
}

void RenderAimSettings()
{
    BeginSettingsCard("##AimSafety", "ZOMBIE AIM - STANDALONE WORLD ONLY", 92.0F);
    ImGui::Checkbox("Enable zombie aimbot", &g_Settings.EnableZombieAimbot);
    ImGui::SameLine();
    ImGui::TextDisabled(g_AimTargetActive.load(std::memory_order_relaxed) ? "TARGET LOCKED" : "NO TARGET");
    ImGui::TextDisabled("Hold RMB to aim. Hipfire mode also allows LMB while firing.");
    EndSettingsCard();

    ImGui::Spacing();
    if (!ImGui::BeginTable("##AimColumns", 2, ImGuiTableFlags_SizingStretchSame))
        return;

    ImGui::TableNextColumn();
    BeginSettingsCard("##AimTargeting", "TARGETING", 246.0F);
    ImGui::Checkbox("Draw FOV circle", &g_Settings.DrawAimFov);
    ImGui::Checkbox("Visibility check", &g_Settings.AimVisibilityCheck);
    ImGui::Checkbox("Hipfire mode", &g_Settings.AimHipfireMode);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat(
        "FOV radius",
        &g_Settings.AimFovPixels,
        kMinAimFovPixels,
        kMaxAimFovPixels,
        "%.0f px",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextDisabled("Selects the nearest living zombie to the crosshair.");
    EndSettingsCard();

    ImGui::TableNextColumn();
    BeginSettingsCard("##AimResponse", "AIM RESPONSE", 246.0F);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat(
        "Aim speed",
        &g_Settings.AimSpeed,
        kMinAimSpeed,
        kMaxAimSpeed,
        "%.1f",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Simple RCS", &g_Settings.EnableSimpleRcs);
    ImGui::BeginDisabled(!g_Settings.EnableSimpleRcs);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("RCS strength", &g_Settings.RcsStrength, 0.0F, 1.0F, "%.2f");
    ImGui::EndDisabled();
    ImGui::TextDisabled("Current recovery boost: %.0f%%", g_Settings.RcsStrength * 100.0F);
    ImGui::TextWrapped("RCS accelerates pitch recovery while LMB is held and a zombie is locked.");
    EndSettingsCard();

    ImGui::EndTable();
}

void RenderLootSettings()
{
    BeginSettingsCard("##LootMaster", "LOOT MASTER", 73.0F);
    ImGui::Checkbox("Enable all loot scanning", &g_Settings.ShowLoot);
    ImGui::SameLine();
    ImGui::TextDisabled("104 concrete SDK pickup classes");
    EndSettingsCard();

    ImGui::Spacing();
    if (!ImGui::BeginTable("##LootColumns", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame))
        return;

    ImGui::TableNextColumn();
    ImGui::Checkbox("Firearms", &g_Settings.ShowFirearms);
    ImGui::Checkbox("Melee weapons", &g_Settings.ShowMelee);
    ImGui::Checkbox("Ammunition", &g_Settings.ShowAmmo);
    ImGui::Checkbox("Medical", &g_Settings.ShowMedical);
    ImGui::Checkbox("Explosives / throwables", &g_Settings.ShowExplosives);
    ImGui::Checkbox("Attachments", &g_Settings.ShowAttachments);

    ImGui::TableNextColumn();
    ImGui::Checkbox("Equipment", &g_Settings.ShowEquipment);
    ImGui::Checkbox("Supplies", &g_Settings.ShowSupplies);
    ImGui::Checkbox("Deployables", &g_Settings.ShowDeployables);
    ImGui::Checkbox("Objective / key items", &g_Settings.ShowObjectiveItems);
    ImGui::Checkbox("Other pickups", &g_Settings.ShowMiscPickups);
    ImGui::TextDisabled("Nearest markers are drawn first.");
    ImGui::EndTable();
}

void RenderAmmoSettings()
{
    BeginSettingsCard("##AmmoMaster", "AMMUNITION MASTER", 73.0F);
    ImGui::Checkbox("Enable ammunition markers", &g_Settings.ShowAmmo);
    ImGui::SameLine();
    ImGui::TextDisabled("Filter individual calibres below");
    EndSettingsCard();

    ImGui::Spacing();
    if (!ImGui::BeginTable("##AmmoColumns", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame))
        return;
    ImGui::TableNextColumn();
    ImGui::Checkbox("9mm", &g_Settings.Ammo9mm);
    ImGui::Checkbox(".45 ACP", &g_Settings.Ammo45Acp);
    ImGui::Checkbox(".22 LR", &g_Settings.Ammo22Lr);
    ImGui::Checkbox(".357 Magnum", &g_Settings.Ammo357);
    ImGui::TableNextColumn();
    ImGui::Checkbox(".308 Winchester", &g_Settings.Ammo308);
    ImGui::Checkbox("5.56mm NATO", &g_Settings.Ammo556);
    ImGui::Checkbox("6.8x51mm", &g_Settings.Ammo68x51);
    ImGui::Checkbox("12 gauge", &g_Settings.Ammo12Gauge);
    ImGui::EndTable();

    ImGui::Spacing();
    BeginSettingsCard("##AmmoTraining", "OFFLINE AMMO TRAINING", 154.0F);
    static constexpr const char* AmmoModes[]{"Disabled", "Infinite reserve", "Bottomless magazine"};
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::Combo("Ammo mode", &g_Settings.AmmoCheatMode, AmmoModes, 3);
    ImGui::Checkbox("Infinite stamina", &g_Settings.InfiniteStamina);
    ImGui::Checkbox("Infinite flashlight battery", &g_Settings.InfiniteFlashlightBattery);
    ImGui::TextDisabled("Uses the game's built-in training/debug commands only in a standalone world.");
    EndSettingsCard();
}

void RenderWorldSettings()
{
    BeginSettingsCard("##WorldMaster", "WORLD MASTER", 73.0F);
    ImGui::Checkbox("Enable world-object scanning", &g_Settings.ShowWorld);
    ImGui::SameLine();
    ImGui::TextDisabled("All loaded UWorld levels");
    EndSettingsCard();

    ImGui::Spacing();
    if (!ImGui::BeginTable("##WorldColumns", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame))
        return;
    ImGui::TableNextColumn();
    ImGui::Checkbox("Loot containers / racks", &g_Settings.ShowContainers);
    ImGui::Checkbox("Objectives", &g_Settings.ShowObjectives);
    ImGui::Checkbox("Extraction points", &g_Settings.ShowExtractions);
    ImGui::Checkbox("Powered interactables", &g_Settings.ShowInteractables);
    ImGui::TableNextColumn();
    ImGui::Checkbox("Doors", &g_Settings.ShowDoors);
    ImGui::Checkbox("Vehicles", &g_Settings.ShowVehicles);
    ImGui::Checkbox("Other SDK actors (debug)", &g_Settings.ShowDebugActors);
    ImGui::TextDisabled("Doors and debug actors default off to reduce noise.");
    ImGui::EndTable();
}

void RenderDamageSettings()
{
    const DamageModifierStatus Status =
        g_DamageModifierStatus.load(std::memory_order_relaxed);
    const bool Applied = Status == DamageModifierStatus::Applied;
    const bool Blocked = Status == DamageModifierStatus::BlockedOnline;

    BeginSettingsCard("##DamageSafety", "SINGLE-PLAYER SAFETY LOCK", 122.0F);
    ImGui::TextColored(
        Applied ? ImVec4(0.35F, 1.0F, 0.72F, 1.0F)
                : (Blocked ? ImVec4(1.0F, 0.35F, 0.35F, 1.0F)
                           : ImVec4(0.94F, 0.76F, 0.34F, 1.0F)),
        "%s",
        DamageModifierStatusText());
    ImGui::TextWrapped(
        "Fail-closed: this page can only change damage while UWorld::NetDriver is null. "
        "Original values are restored when disabled, blocked, or unloaded.");
    EndSettingsCard();

    ImGui::Spacing();
    BeginSettingsCard("##DamageControls", "OUTGOING PLAYER DAMAGE", 218.0F);
    ImGui::Checkbox("Enable offline damage modifier", &g_Settings.EnableOfflineDamageModifier);
    ImGui::Spacing();
    ImGui::BeginDisabled(!g_Settings.EnableOfflineDamageModifier);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat(
        "Damage multiplier",
        &g_Settings.OfflineDamageMultiplier,
        kMinDamageMultiplier,
        kMaxDamageMultiplier,
        "%.2f x",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Include stability / stagger damage", &g_Settings.IncludeStabilityDamage);
    if (ImGui::Button("SET 1.00x", ImVec2(118.0F, 32.0F)))
        g_Settings.OfflineDamageMultiplier = 1.0F;
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextDisabled("Covers every firearm hit zone and all melee weapons.");
    ImGui::TextDisabled("Default: disabled. Range: 0.25x to 20.00x.");
    EndSettingsCard();

    ImGui::Spacing();
    if (!ImGui::BeginTable("##WeaponColumns", 2, ImGuiTableFlags_SizingStretchSame))
        return;
    ImGui::TableNextColumn();
    BeginSettingsCard("##WeaponHandling", "WEAPON HANDLING", 270.0F);
    ImGui::Checkbox("No recoil", &g_Settings.NoRecoil);
    ImGui::Checkbox("Adjust weapon spread", &g_Settings.ModifyWeaponSpread);
    ImGui::BeginDisabled(!g_Settings.ModifyWeaponSpread);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Spread multiplier", &g_Settings.WeaponSpreadMultiplier, 0.0F, 2.0F, "%.2f x");
    ImGui::EndDisabled();
    ImGui::Checkbox("Modify reload speed", &g_Settings.ModifyReloadSpeed);
    ImGui::BeginDisabled(!g_Settings.ModifyReloadSpeed);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Reload multiplier", &g_Settings.ReloadSpeedMultiplier, kMinWeaponSpeedMultiplier, kMaxWeaponSpeedMultiplier, "%.2f x");
    ImGui::EndDisabled();
    ImGui::Checkbox("Modify fire / attack rate", &g_Settings.ModifyFireRate);
    ImGui::BeginDisabled(!g_Settings.ModifyFireRate);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Fire-rate multiplier", &g_Settings.FireRateMultiplier, kMinWeaponSpeedMultiplier, kMaxWeaponSpeedMultiplier, "%.2f x");
    ImGui::EndDisabled();
    EndSettingsCard();

    ImGui::TableNextColumn();
    BeginSettingsCard("##Survival", "SURVIVAL", 270.0F);
    ImGui::Checkbox("God mode", &g_Settings.GodMode);
    ImGui::Checkbox("Automatic health regeneration", &g_Settings.AutoHealthRegeneration);
    ImGui::Checkbox("Infinite stamina", &g_Settings.InfiniteStamina);
    ImGui::Checkbox("Infinite flashlight battery", &g_Settings.InfiniteFlashlightBattery);
    ImGui::Spacing();
    ImGui::TextWrapped("God mode temporarily disables damage on the local pawn. Auto regeneration restores health once per second.");
    ImGui::Spacing();
    ImGui::TextDisabled("All attribute edits are snapshotted and restored.");
    EndSettingsCard();
    ImGui::EndTable();
}

void RenderToolsSettings()
{
    BeginSettingsCard("##ToolSafety", "STANDALONE ACTIONS", 92.0F);
    ImGui::TextColored(
        g_OfflineAllowed.load(std::memory_order_relaxed)
            ? ImVec4(0.35F, 1.0F, 0.72F, 1.0F)
            : ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
        "%s",
        g_OfflineAllowed.load(std::memory_order_relaxed)
            ? "STANDALONE WORLD VERIFIED"
            : "LOCKED - NO STANDALONE WORLD");
    ImGui::TextDisabled("Requests are discarded if a NetDriver is attached.");
    EndSettingsCard();

    ImGui::Spacing();
    if (!ImGui::BeginTable("##ToolColumns", 2, ImGuiTableFlags_SizingStretchSame))
        return;
    ImGui::TableNextColumn();
    BeginSettingsCard("##AiTimeTools", "AI / TIME", 250.0F);
    ImGui::Checkbox("Zombie time control", &g_Settings.ModifyZombieTime);
    ImGui::BeginDisabled(!g_Settings.ModifyZombieTime);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Zombie time scale", &g_Settings.ZombieTimeScale, 0.0F, 2.0F, "%.2f x");
    ImGui::EndDisabled();
    ImGui::Checkbox("Photo mode + free camera", &g_Settings.PhotoMode);
    ImGui::BeginDisabled(!g_Settings.PhotoMode);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Photo time scale", &g_Settings.PhotoTimeScale, kMinGlobalTimeScale, 1.0F, "%.2f x");
    ImGui::EndDisabled();
    ImGui::Checkbox("Global time control", &g_Settings.ModifyGlobalTime);
    ImGui::BeginDisabled(!g_Settings.ModifyGlobalTime);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Global time scale", &g_Settings.GlobalTimeScale, kMinGlobalTimeScale, kMaxGlobalTimeScale, "%.2f x");
    ImGui::EndDisabled();
    if (ImGui::Button("FREEZE ZOMBIES", ImVec2(-1.0F, 32.0F)))
    {
        g_Settings.ModifyZombieTime = true;
        g_Settings.ZombieTimeScale = 0.0F;
    }
    if (ImGui::Button("DESTROY ALL AI", ImVec2(-1.0F, 32.0F)))
        g_RequestDestroyAllAi.store(true, std::memory_order_release);
    EndSettingsCard();

    ImGui::TableNextColumn();
    BeginSettingsCard("##TeleportTools", "TELEPORT / PICKUPS", 250.0F);
    if (ImGui::Button("TELEPORT TO NEAREST OBJECTIVE", ImVec2(-1.0F, 32.0F)))
        g_RequestTeleportObjective.store(true, std::memory_order_release);
    if (ImGui::Button("TELEPORT TO NEAREST EXTRACTION", ImVec2(-1.0F, 32.0F)))
        g_RequestTeleportExtraction.store(true, std::memory_order_release);
    if (ImGui::Button("TELEPORT TO CROSSHAIR PICKUP", ImVec2(-1.0F, 32.0F)))
        g_RequestTeleportPickup.store(true, std::memory_order_release);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderScalar("Vacuum radius", ImGuiDataType_Double, &g_Settings.PickupVacuumRadiusMeters, &kMinRadiusMeters, &kMaxRadiusMeters, "%.0f m");
    if (ImGui::Button("VACUUM PICKUPS", ImVec2(-1.0F, 32.0F)))
        g_RequestPickupVacuum.store(true, std::memory_order_release);
    ImGui::TextDisabled("Pickup vacuum is a one-shot world edit.");
    EndSettingsCard();
    ImGui::EndTable();

    ImGui::Spacing();
    if (!ImGui::BeginTable("##GrantColumns", 2, ImGuiTableFlags_SizingStretchSame))
        return;
    ImGui::TableNextColumn();
    BeginSettingsCard("##SpawnTools", "ITEM SPAWNER", 190.0F);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##SpawnIdentifier", "SDK item identifier", g_Settings.SpawnItemIdentifier, sizeof(g_Settings.SpawnItemIdentifier));
    if (ImGui::Button("SPAWN ITEM", ImVec2(-1.0F, 34.0F)))
        g_RequestSpawnItem.store(true, std::memory_order_release);
    ImGui::TextWrapped("Enter the identifier expected by KCheatManager::DebugSpawnItem.");
    EndSettingsCard();

    ImGui::TableNextColumn();
    BeginSettingsCard("##GrantTools", "AMMO / SUPPLY GRANTS", 190.0F);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##AmmoIdentifier", "Ammo type identifier", g_Settings.AmmoGrantIdentifier, sizeof(g_Settings.AmmoGrantIdentifier));
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputInt("Ammo amount", &g_Settings.GiveAmmoAmount, 10, 100);
    if (ImGui::Button("GIVE AMMO", ImVec2(-1.0F, 32.0F)))
        g_RequestGiveAmmo.store(true, std::memory_order_release);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputInt("Supplies", &g_Settings.GiveSuppliesAmount, 10, 100);
    if (ImGui::Button("GIVE SUPPLIES", ImVec2(-1.0F, 32.0F)))
        g_RequestGiveSupplies.store(true, std::memory_order_release);
    EndSettingsCard();
    ImGui::EndTable();
}

void RenderThemeSettings()
{
    BeginSettingsCard("##ThemeColors", "MENU COLORS", 285.0F);
    ImGui::ColorEdit4("Accent", g_Settings.MenuAccent, ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Background", g_Settings.MenuBackground, ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Panels", g_Settings.MenuPanel, ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Text", g_Settings.MenuText, ImGuiColorEditFlags_AlphaBar);
    ImGui::Checkbox("Animated rainbow accent", &g_Settings.RainbowAccent);
    ImGui::BeginDisabled(!g_Settings.RainbowAccent);
    ImGui::SliderFloat("Rainbow speed", &g_Settings.RainbowSpeed, 0.05F, 1.0F, "%.2f");
    ImGui::EndDisabled();
    if (ImGui::Button("RESET THEME", ImVec2(150.0F, 34.0F)))
    {
        const OverlaySettings Defaults{};
        std::copy(std::begin(Defaults.MenuAccent), std::end(Defaults.MenuAccent), std::begin(g_Settings.MenuAccent));
        std::copy(std::begin(Defaults.MenuBackground), std::end(Defaults.MenuBackground), std::begin(g_Settings.MenuBackground));
        std::copy(std::begin(Defaults.MenuPanel), std::end(Defaults.MenuPanel), std::begin(g_Settings.MenuPanel));
        std::copy(std::begin(Defaults.MenuText), std::end(Defaults.MenuText), std::begin(g_Settings.MenuText));
        g_Settings.RainbowAccent = Defaults.RainbowAccent;
        g_Settings.RainbowSpeed = Defaults.RainbowSpeed;
    }
    EndSettingsCard();

    ImGui::Spacing();
    BeginSettingsCard("##CharacterColors", "CHARACTER / ALERT COLORS", 250.0F);
    ImGui::ColorEdit4("Players", g_Settings.PlayerEspColor, ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Zombies", g_Settings.ZombieEspColor, ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Skeleton", g_Settings.SkeletonColor, ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Head marker", g_Settings.HeadMarkerColor, ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Snapline", g_Settings.SnaplineColor, ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Threat", g_Settings.ThreatColor, ImGuiColorEditFlags_AlphaBar);
    EndSettingsCard();

    ImGui::Spacing();
    BeginSettingsCard("##CategoryColors", "LOOT / WORLD CATEGORY COLORS", 610.0F);
    static constexpr std::array<const char*, static_cast<std::size_t>(MarkerCategory::Count)> Names{
        "Fallback", "Firearm", "Melee", "Ammo", "Medical", "Explosive", "Attachment",
        "Equipment", "Supply", "Deployable", "Objective item", "Misc pickup", "Container",
        "Objective", "Extraction", "Interactable", "Door", "Vehicle", "Debug actor"};
    for (std::size_t Index = 1; Index < Names.size(); ++Index)
    {
        ImGui::PushID(static_cast<int>(Index));
        ImGui::ColorEdit4(Names[Index], g_Settings.CategoryColors[Index].data(), ImGuiColorEditFlags_AlphaBar);
        ImGui::PopID();
    }
    EndSettingsCard();

    ImGui::Spacing();
    BeginSettingsCard("##ThemePreview", "LIVE PREVIEW", 145.0F);
    ImGui::Text("The entire panel updates immediately.");
    ImGui::Button("BUTTON PREVIEW", ImVec2(180.0F, 34.0F));
    ImGui::SameLine();
    static bool PreviewToggle = true;
    ImGui::Checkbox("Toggle preview", &PreviewToggle);
    ImGui::TextDisabled("Theme settings are reset with Reset Defaults.");
    EndSettingsCard();
}

void RenderHotkeyCombo(const char* Label, int* Value)
{
    static constexpr std::array<const char*, 20> Names{
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
        "Insert", "Delete", "Home", "End", "Page Up", "Page Down", "Numpad +", "Numpad -"};
    static constexpr std::array<int, 20> Keys{
        VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12,
        VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT, VK_ADD, VK_SUBTRACT};
    int Selected = 0;
    for (std::size_t Index = 0; Index < Keys.size(); ++Index)
    {
        if (Value && *Value == Keys[Index])
            Selected = static_cast<int>(Index);
    }
    ImGui::SetNextItemWidth(-1.0F);
    if (Value && ImGui::Combo(Label, &Selected, Names.data(), static_cast<int>(Names.size())))
        *Value = Keys[static_cast<std::size_t>(Selected)];
}

void RenderInspectorSettings()
{
    BeginSettingsCard("##InspectorControls", "SEARCHABLE SDK ACTOR INSPECTOR", 142.0F);
    ImGui::Checkbox("Capture actor snapshots", &g_Settings.EnableActorInspector);
    ImGui::Checkbox("Include unclassified SDK actors", &g_Settings.InspectorIncludeAllActors);
    ImGui::SliderInt("Snapshot limit", &g_Settings.InspectorMaxEntries, 100, 2500, "%d actors");
    static char Search[128]{};
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputTextWithHint("##ActorSearch", "Search SDK class name", Search, sizeof(Search));
    EndSettingsCard();

    std::vector<InspectorActorEntry> Snapshot;
    {
        std::scoped_lock Lock(g_InspectorMutex);
        Snapshot = g_InspectorSnapshot;
    }
    std::string SearchLower(Search);
    std::transform(SearchLower.begin(), SearchLower.end(), SearchLower.begin(),
        [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
    ImGui::TextDisabled("Snapshot: %llu actors; data is copied on the game thread before this page reads it.",
        static_cast<unsigned long long>(Snapshot.size()));
    if (ImGui::BeginTable("##ActorInspectorTable", 6,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0.0F, 275.0F)))
    {
        ImGui::TableSetupColumn("Address"); ImGui::TableSetupColumn("Category");
        ImGui::TableSetupColumn("SDK class"); ImGui::TableSetupColumn("Distance");
        ImGui::TableSetupColumn("X / Y / Z"); ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        for (const InspectorActorEntry& Entry : Snapshot)
        {
            std::string ClassLower = Entry.ClassName;
            std::transform(ClassLower.begin(), ClassLower.end(), ClassLower.begin(),
                [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
            if (!SearchLower.empty() && ClassLower.find(SearchLower) == std::string::npos)
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("0x%llX", static_cast<unsigned long long>(Entry.Address));
            ImGui::TableNextColumn(); ImGui::Text("%ls", CategoryLabel(Entry.Category));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Entry.ClassName.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%.0f m", Entry.Distance);
            ImGui::TableNextColumn(); ImGui::Text("%.0f / %.0f / %.0f", Entry.Location.X, Entry.Location.Y, Entry.Location.Z);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Entry.Alive ? "alive" : "object");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    BeginSettingsCard("##DiagnosticsConsole", "ON-SCREEN DIAGNOSTIC CONSOLE", 330.0F);
    ImGui::Checkbox("Show last lines on HUD", &g_Settings.ShowDiagnosticOverlay);
    ImGui::SameLine(); ImGui::SliderInt("Lines", &g_Settings.DiagnosticOverlayLines, 3, 15);
    std::vector<std::string> Lines;
    {
        std::scoped_lock Lock(g_DiagnosticConsoleMutex);
        Lines.assign(g_DiagnosticConsole.begin(), g_DiagnosticConsole.end());
    }
    if (ImGui::Button("CLEAR CONSOLE", ImVec2(140.0F, 30.0F)))
    {
        std::scoped_lock Lock(g_DiagnosticConsoleMutex);
        g_DiagnosticConsole.clear();
        Lines.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("offline=%s  actor_scan=%s  photo=%s  modifiers=%s",
        g_OfflineAllowed.load() ? "yes" : "no",
        g_ActorScanUnlockedLogged.load() ? "ready" : "warmup",
        g_PhotoModeApplied.load() ? "on" : "off",
        AnyGameplayModifierApplied() ? "active" : "idle");
    ImGui::BeginChild("##ConsoleLines", ImVec2(0.0F, 225.0F), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& Line : Lines)
        ImGui::TextUnformatted(Line.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 8.0F)
        ImGui::SetScrollHereY(1.0F);
    ImGui::EndChild();
    EndSettingsCard();
}

void RenderConfigSettings()
{
    BeginSettingsCard("##Presets", "ONE-CLICK VISUAL PRESETS", 112.0F);
    if (ImGui::Button("COMBAT", ImVec2(150.0F, 34.0F))) ApplyVisualPreset(g_Settings, 0);
    ImGui::SameLine(); if (ImGui::Button("LOOT SEARCH", ImVec2(150.0F, 34.0F))) ApplyVisualPreset(g_Settings, 1);
    ImGui::SameLine(); if (ImGui::Button("EXPLORATION", ImVec2(150.0F, 34.0F))) ApplyVisualPreset(g_Settings, 2);
    ImGui::TextDisabled("Presets only change visual scanning and HUD settings.");
    EndSettingsCard();

    ImGui::Spacing();
    BeginSettingsCard("##Profiles", "JSON CONFIGURATION PROFILES", 170.0F);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::Combo("Profile", &g_Settings.SelectedProfile, kProfileNames.data(), static_cast<int>(kProfileNames.size()));
    static std::string ProfileStatus;
    if (ImGui::Button("SAVE PROFILE", ImVec2(155.0F, 34.0F)))
        ProfileStatus = SaveProfileJson(g_Settings.SelectedProfile, g_Settings) ? "Saved JSON profile." : "Could not save profile.";
    ImGui::SameLine();
    if (ImGui::Button("LOAD PROFILE", ImVec2(155.0F, 34.0F)))
        ProfileStatus = LoadProfileJson(g_Settings.SelectedProfile, &g_Settings) ? "Loaded JSON profile." : "Profile does not exist yet.";
    ImGui::TextDisabled("%s", ProfileStatus.empty() ? "Profiles are stored beside the DLL." : ProfileStatus.c_str());
    EndSettingsCard();

    ImGui::Spacing();
    BeginSettingsCard("##Hotkeys", "HOTKEY REBINDING", 330.0F);
    if (ImGui::BeginTable("##HotkeyColumns", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        RenderHotkeyCombo("Menu", &g_Settings.MenuHotkey);
        RenderHotkeyCombo("Overlay", &g_Settings.OverlayHotkey);
        RenderHotkeyCombo("Next page", &g_Settings.NextPageHotkey);
        ImGui::TableNextColumn();
        RenderHotkeyCombo("Unload", &g_Settings.UnloadHotkey);
        RenderHotkeyCombo("Tele-kill target", &g_Settings.TeleKillSingleHotkey);
        RenderHotkeyCombo("Tele-kill all", &g_Settings.TeleKillAllHotkey);
        ImGui::EndTable();
    }
    ImGui::TextWrapped("Choose distinct keys. Rebindings take effect immediately and are included in JSON profiles.");
    EndSettingsCard();
}

void RenderMiscSettings()
{
    BeginSettingsCard("##MiscSafety", "SINGLE-PLAYER SAFETY LOCK", 92.0F);
    ImGui::TextColored(
        g_OfflineAllowed.load(std::memory_order_relaxed)
            ? ImVec4(0.35F, 1.0F, 0.72F, 1.0F)
            : ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
        "%s",
        g_OfflineAllowed.load(std::memory_order_relaxed)
            ? "STANDALONE WORLD VERIFIED"
            : "LOCKED - NO STANDALONE WORLD");
    ImGui::TextDisabled("Every modifier restores when disabled, unloaded, or a NetDriver appears.");
    EndSettingsCard();

    ImGui::Spacing();
    if (!ImGui::BeginTable("##MiscColumns", 2, ImGuiTableFlags_SizingStretchSame))
        return;

    ImGui::TableNextColumn();
    BeginSettingsCard("##CombatMisc", "COMBAT", 245.0F);
    ImGui::Checkbox("One-hit melee", &g_Settings.OneHitMelee);
    ImGui::Checkbox("Double melee range", &g_Settings.DoubleMeleeRange);
    ImGui::Spacing();
    if (ImGui::Button("TELE-KILL TARGET", ImVec2(-1.0F, 34.0F)))
        g_RequestTeleKillSingle.store(true, std::memory_order_release);
    if (ImGui::Button("TELE-KILL ALL LOADED ZOMBIES", ImVec2(-1.0F, 34.0F)))
        g_RequestTeleKillAll.store(true, std::memory_order_release);
    ImGui::TextDisabled("Defaults: F6 target  |  F7 all loaded zombies; rebind on CONFIG");
    ImGui::TextDisabled("Target = zombie nearest the crosshair.");
    EndSettingsCard();

    ImGui::TableNextColumn();
    BeginSettingsCard("##WorldMisc", "PLAYER / WORLD", 245.0F);
    ImGui::Checkbox("Fullbright", &g_Settings.Fullbright);
    ImGui::BeginDisabled(!g_Settings.Fullbright);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat("Brightness", &g_Settings.FullbrightIntensity, 1.0F, 6.0F, "%.1f");
    ImGui::EndDisabled();
    ImGui::Checkbox("Ghost mode (fly + AI ignore)", &g_Settings.GhostMode);
    ImGui::Checkbox("Player speed / attack speed", &g_Settings.SpeedHack);
    ImGui::BeginDisabled(!g_Settings.SpeedHack);
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::SliderFloat(
        "Speed multiplier",
        &g_Settings.SpeedMultiplier,
        kMinSpeedMultiplier,
        kMaxSpeedMultiplier,
        "%.2f x",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    EndSettingsCard();

    ImGui::EndTable();
}

void RenderImGuiSettingsMenu()
{
    ApplyDynamicMenuColors();
    ImGuiIO& Io = ImGui::GetIO();
    constexpr float MinimumWidth = 560.0F;
    constexpr float MinimumHeight = 480.0F;
    const float MaximumWidth = std::max(MinimumWidth, Io.DisplaySize.x - 24.0F);
    const float MaximumHeight = std::max(MinimumHeight, Io.DisplaySize.y - 24.0F);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(MinimumWidth, MinimumHeight),
        ImVec2(MaximumWidth, MaximumHeight));
    if (!g_MenuPlacementInitialized)
    {
        const float DefaultWidth = std::min(790.0F, MaximumWidth);
        const float DefaultHeight = std::min(660.0F, MaximumHeight);
        ImGui::SetNextWindowPos(
            ImVec2(Io.DisplaySize.x * 0.5F, Io.DisplaySize.y * 0.5F),
            ImGuiCond_Always,
            ImVec2(0.5F, 0.5F));
        ImGui::SetNextWindowSize(ImVec2(DefaultWidth, DefaultHeight), ImGuiCond_Always);
        g_MenuPlacementInitialized = true;
    }

    constexpr ImGuiWindowFlags Flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("NMRiH2 Offline Inspector##Main", nullptr, Flags))
    {
        ImGui::End();
        return;
    }

    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const ImVec2 WindowPosition = ImGui::GetWindowPos();
    const ImVec2 WindowSize = ImGui::GetWindowSize();
    const ImVec4 Accent = CurrentMenuAccent();
    const ImU32 AccentColor = ImGui::ColorConvertFloat4ToU32(Accent);
    const ImU32 AccentDark = ImGui::ColorConvertFloat4ToU32(ScaledColor(Accent, 0.18F, 1.0F));
    const ImU32 AccentMid = ImGui::ColorConvertFloat4ToU32(ScaledColor(Accent, 0.32F, 1.0F));
    DrawList->AddRectFilledMultiColor(
        WindowPosition,
        ImVec2(WindowPosition.x + WindowSize.x, WindowPosition.y + 88.0F),
        AccentDark,
        AccentMid,
        AccentDark,
        ImGui::ColorConvertFloat4ToU32(MenuColor(g_Settings.MenuBackground)));
    DrawList->AddRectFilled(
        ImVec2(WindowPosition.x, WindowPosition.y + 86.0F),
        ImVec2(WindowPosition.x + WindowSize.x, WindowPosition.y + 89.0F),
        AccentColor);

    ImGui::SetCursorPos(ImVec2(24.0F, 17.0F));
    if (g_TitleFont)
        ImGui::PushFont(g_TitleFont);
    ImGui::TextColored(MenuColor(g_Settings.MenuText), "NMRiH2  OFFLINE INSPECTOR");
    if (g_TitleFont)
        ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(25.0F, 54.0F));
    ImGui::TextColored(ImVec4(0.44F, 0.67F, 0.82F, 1.00F), "PRIVATE SESSION TOOLKIT  /  UE 5.6.1");

    const UINT64 UnlockTick = g_ActorScanUnlockTick.load(std::memory_order_relaxed);
    const bool ActorWarmup = UnlockTick != 0 && GetTickCount64() < UnlockTick;
    const char* StatusText = !g_OverlayEnabled.load(std::memory_order_relaxed)
        ? "PAUSED"
        : (ActorWarmup ? "OFFLINE  |  WARMUP" : "OFFLINE  |  ACTIVE");
    const ImVec2 StatusSize = ImGui::CalcTextSize(StatusText);
    const ImVec2 PillMin(WindowPosition.x + WindowSize.x - StatusSize.x - 49.0F, WindowPosition.y + 25.0F);
    const ImVec2 PillMax(WindowPosition.x + WindowSize.x - 22.0F, WindowPosition.y + 57.0F);
    DrawList->AddRectFilled(PillMin, PillMax, IM_COL32(13, 112, 113, 235), 16.0F);
    DrawList->AddCircleFilled(ImVec2(PillMin.x + 15.0F, PillMin.y + 16.0F), 4.0F, IM_COL32(90, 255, 205, 255));
    DrawList->AddText(ImVec2(PillMin.x + 26.0F, PillMin.y + 8.0F), IM_COL32(214, 255, 244, 255), StatusText);

    ImGui::SetCursorPosY(103.0F);
    constexpr std::array<const char*, 11> TabNames{
        "GENERAL", "AIM", "LOOT", "AMMO", "WORLD", "DAMAGE", "MISC", "TOOLS",
        "VISUALS", "INSPECTOR", "CONFIG"};
    constexpr int TabsPerRow = 6;
    const float TabGap = ImGui::GetStyle().ItemSpacing.x;
    const float TabWidth =
        (ImGui::GetContentRegionAvail().x - (TabGap * static_cast<float>(TabsPerRow - 1))) /
        static_cast<float>(TabsPerRow);
    for (int Index = 0; Index < static_cast<int>(TabNames.size()); ++Index)
    {
        if (Index > 0 && (Index % TabsPerRow) != 0)
            ImGui::SameLine();

        const bool Active = static_cast<int>(g_Settings.ActiveTab) == Index;
        ImGui::PushStyleColor(ImGuiCol_Button, Active ? ScaledColor(Accent, 0.64F) : ImVec4(0.04F, 0.09F, 0.15F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ScaledColor(Accent, 0.78F));
        ImGui::PushStyleColor(ImGuiCol_Text, Active ? ImVec4(1.0F, 1.0F, 1.0F, 1.0F) : ImVec4(0.52F, 0.63F, 0.75F, 1.0F));
        ImGui::PushID(Index);
        if (ImGui::Button(TabNames[static_cast<std::size_t>(Index)], ImVec2(TabWidth, 38.0F)))
            g_Settings.ActiveTab = static_cast<MenuTab>(Index);
        ImGui::PopID();
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();
    ImGui::BeginChild("##PageContent", ImVec2(0.0F, -96.0F), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    switch (g_Settings.ActiveTab)
    {
    case MenuTab::General: RenderGeneralSettings(); break;
    case MenuTab::Aim: RenderAimSettings(); break;
    case MenuTab::Loot: RenderLootSettings(); break;
    case MenuTab::Ammo: RenderAmmoSettings(); break;
    case MenuTab::World: RenderWorldSettings(); break;
    case MenuTab::Damage: RenderDamageSettings(); break;
    case MenuTab::Misc: RenderMiscSettings(); break;
    case MenuTab::Tools: RenderToolsSettings(); break;
    case MenuTab::Theme: RenderThemeSettings(); break;
    case MenuTab::Inspector: RenderInspectorSettings(); break;
    case MenuTab::Config: RenderConfigSettings(); break;
    default: break;
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("RESET DEFAULTS", ImVec2(132.0F, 32.0F)))
    {
        const MenuTab CurrentTab = g_Settings.ActiveTab;
        g_Settings = OverlaySettings{};
        g_Settings.ActiveTab = CurrentTab;
    }
    ImGui::SameLine();
    if (ImGui::Button("OPEN DISCORD", ImVec2(128.0F, 32.0F)))
        ShellExecuteW(nullptr, L"open", kDiscordInvite, nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (ImGui::Button("COPY INVITE", ImVec2(118.0F, 32.0F)))
        ImGui::SetClipboardText("https://discord.gg/nKDN7vCFE7");
    ImGui::TextDisabled("Drag the panel to move it  |  Drag an edge or corner to resize");
    ImGui::TextDisabled("Default: F1 hide  |  PAGE UP next page  |  INSERT overlay  |  END unload");
    ImGui::TextDisabled("All hotkeys can be rebound on CONFIG.");

    ImGui::End();
}
#endif

#ifndef OVERLAY_SOURCE_CHECK
void AllocateShaderDescriptor(
    ImGui_ImplDX12_InitInfo* Info,
    D3D12_CPU_DESCRIPTOR_HANDLE* OutCpu,
    D3D12_GPU_DESCRIPTOR_HANDLE* OutGpu)
{
    auto* Allocator = static_cast<ShaderDescriptorAllocator*>(Info ? Info->UserData : nullptr);
    if (!Allocator || !OutCpu || !OutGpu || Allocator->FreeIndices.empty())
    {
        if (OutCpu)
            *OutCpu = {};
        if (OutGpu)
            *OutGpu = {};
        return;
    }

    const UINT Index = Allocator->FreeIndices.back();
    Allocator->FreeIndices.pop_back();
    OutCpu->ptr = Allocator->CpuStart.ptr + (static_cast<SIZE_T>(Index) * Allocator->Increment);
    OutGpu->ptr = Allocator->GpuStart.ptr + (static_cast<UINT64>(Index) * Allocator->Increment);
}

void FreeShaderDescriptor(
    ImGui_ImplDX12_InitInfo* Info,
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE)
{
    auto* Allocator = static_cast<ShaderDescriptorAllocator*>(Info ? Info->UserData : nullptr);
    if (!Allocator || Allocator->Increment == 0 || Cpu.ptr < Allocator->CpuStart.ptr)
        return;

    const SIZE_T Delta = Cpu.ptr - Allocator->CpuStart.ptr;
    if ((Delta % Allocator->Increment) != 0)
        return;

    const UINT Index = static_cast<UINT>(Delta / Allocator->Increment);
    if (Index < 64)
        Allocator->FreeIndices.push_back(Index);
}

bool WaitForImGuiGpuLocked(DWORD TimeoutMilliseconds = 3000)
{
    if (!g_RenderQueue || !g_ImGuiFence || !g_ImGuiFenceEvent)
        return true;

    const UINT64 FenceValue = ++g_LastFenceValue;
    if (FAILED(g_RenderQueue->Signal(g_ImGuiFence.Get(), FenceValue)))
        return false;

    if (g_ImGuiFence->GetCompletedValue() < FenceValue &&
        SUCCEEDED(g_ImGuiFence->SetEventOnCompletion(FenceValue, g_ImGuiFenceEvent)))
    {
        return WaitForSingleObject(g_ImGuiFenceEvent, TimeoutMilliseconds) == WAIT_OBJECT_0;
    }

    return g_ImGuiFence->GetCompletedValue() >= FenceValue;
}

void RestoreWindowProcedureLocked()
{
    if (g_GameWindow && g_OriginalWindowProc && IsWindow(g_GameWindow))
    {
        SetWindowLongPtrW(
            g_GameWindow,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(g_OriginalWindowProc));
    }
    g_OriginalWindowProc = nullptr;
    g_GameWindow = nullptr;
}

void SuspendImGuiLocked(const char* Reason)
{
    WriteDiagnosticLog("ImGui suspended: %s", Reason ? Reason : "unspecified failure");
    g_ImGuiReady.store(false, std::memory_order_release);
    g_ImGuiFailed.store(true, std::memory_order_release);
    if (ImGui::GetCurrentContext())
        ImGui::GetIO().MouseDrawCursor = false;
    RestoreWindowProcedureLocked();
}

void ReleaseImGuiResourcesLocked()
{
    g_ImGuiReady.store(false, std::memory_order_release);
    RestoreWindowProcedureLocked();
    WaitForImGuiGpuLocked(INFINITE);

    if (g_Dx12BackendReady)
        ImGui_ImplDX12_Shutdown();
    if (g_Win32BackendReady)
        ImGui_ImplWin32_Shutdown();
    if (ImGui::GetCurrentContext())
        ImGui::DestroyContext();

    g_Dx12BackendReady = false;
    g_Win32BackendReady = false;
    g_TitleFont = nullptr;
    g_MenuPlacementInitialized = false;
    g_Dx12Frames.clear();
    g_ShaderDescriptors = {};
    g_ImGuiCommandList.Reset();
    g_RenderTargetHeap.Reset();
    g_ShaderDescriptorHeap.Reset();
    g_ImGuiFence.Reset();
    g_ImGuiSwapChain.Reset();
    g_ImGuiDevice.Reset();
    g_RenderQueue.Reset();
    g_LastFenceValue = 0;
    if (g_ImGuiFenceEvent)
    {
        CloseHandle(g_ImGuiFenceEvent);
        g_ImGuiFenceEvent = nullptr;
    }
}

LRESULT CALLBACK HookedWindowProcedure(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    const ActiveHookCall ActiveCall{};
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ImGuiWindowProc);
    WNDPROC Original{};
    bool Capture = false;

    {
        std::scoped_lock Lock(g_ImGuiMutex);
        Original = g_OriginalWindowProc;
        const bool MenuVisible =
            g_ImGuiReady.load(std::memory_order_acquire) &&
            g_MenuOpen.load(std::memory_order_relaxed) &&
            g_OverlayEnabled.load(std::memory_order_relaxed) &&
            g_OfflineAllowed.load(std::memory_order_relaxed);

        if (MenuVisible && ImGui::GetCurrentContext())
        {
            const bool Handled = ImGui_ImplWin32_WndProcHandler(Window, Message, WParam, LParam) != 0;
            const ImGuiIO& Io = ImGui::GetIO();
            Capture = Handled || Io.WantCaptureMouse || Io.WantCaptureKeyboard || Io.WantTextInput;
        }
    }

    if (Capture)
        return 1;
    return Original
        ? CallWindowProcW(Original, Window, Message, WParam, LParam)
        : DefWindowProcW(Window, Message, WParam, LParam);
}

bool InitializeImGuiForSwapChain(IDXGISwapChain* SwapChain)
{
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ImGuiInitialize);
    if (!SwapChain)
        return false;

    Microsoft::WRL::ComPtr<IDXGISwapChain3> SwapChain3;
    Microsoft::WRL::ComPtr<ID3D12Device> Device;
    if (FAILED(SwapChain->QueryInterface(IID_PPV_ARGS(SwapChain3.ReleaseAndGetAddressOf()))) ||
        FAILED(SwapChain->GetDevice(IID_PPV_ARGS(Device.ReleaseAndGetAddressOf()))))
    {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC SwapChainDescription{};
    if (FAILED(SwapChain->GetDesc(&SwapChainDescription)) ||
        !SwapChainDescription.OutputWindow ||
        SwapChainDescription.BufferCount < 2 ||
        SwapChainDescription.BufferCount > 8)
    {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> FirstQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> PresentThreadQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> LatestQueue;
    {
        std::scoped_lock QueueLock(g_QueueMutex);
        FirstQueue = g_FirstDirectQueue;
        LatestQueue = g_LatestDirectQueue;
        if (g_ThreadDirectQueue)
            PresentThreadQueue = g_ThreadDirectQueue;
    }

    const auto MatchesSwapChainDevice = [&](ID3D12CommandQueue* Candidate)
    {
        if (!Candidate)
            return false;
        Microsoft::WRL::ComPtr<ID3D12Device> CandidateDevice;
        return SUCCEEDED(Candidate->GetDevice(
                   IID_PPV_ARGS(CandidateDevice.ReleaseAndGetAddressOf()))) &&
               CandidateDevice.Get() == Device.Get();
    };

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue;
    const char* QueueSource = "none";
    if (MatchesSwapChainDevice(FirstQueue.Get()))
    {
        Queue = FirstQueue;
        QueueSource = "first-game-queue";
    }
    else if (MatchesSwapChainDevice(PresentThreadQueue.Get()))
    {
        Queue = PresentThreadQueue;
        QueueSource = "present-thread-fallback";
    }
    else if (MatchesSwapChainDevice(LatestQueue.Get()))
    {
        Queue = LatestQueue;
        QueueSource = "latest-game-fallback";
    }
    else
    {
        return false;
    }

    ReleaseImGuiResourcesLocked();
    g_ImGuiDevice = Device;
    g_ImGuiSwapChain = SwapChain3;
    g_RenderQueue = Queue;
    g_GameWindow = SwapChainDescription.OutputWindow;

    D3D12_DESCRIPTOR_HEAP_DESC RenderTargetHeapDescription{};
    RenderTargetHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    RenderTargetHeapDescription.NumDescriptors = SwapChainDescription.BufferCount;
    if (FAILED(Device->CreateDescriptorHeap(
            &RenderTargetHeapDescription,
            IID_PPV_ARGS(g_RenderTargetHeap.ReleaseAndGetAddressOf()))))
    {
        ReleaseImGuiResourcesLocked();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC ShaderHeapDescription{};
    ShaderHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    ShaderHeapDescription.NumDescriptors = 64;
    ShaderHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(Device->CreateDescriptorHeap(
            &ShaderHeapDescription,
            IID_PPV_ARGS(g_ShaderDescriptorHeap.ReleaseAndGetAddressOf()))))
    {
        ReleaseImGuiResourcesLocked();
        return false;
    }

    g_Dx12Frames.resize(SwapChainDescription.BufferCount);
    const UINT RenderTargetIncrement = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE RenderTarget = g_RenderTargetHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT Index = 0; Index < SwapChainDescription.BufferCount; ++Index)
    {
        Dx12FrameContext& Frame = g_Dx12Frames[Index];
        if (FAILED(Device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(Frame.Allocator.ReleaseAndGetAddressOf()))) ||
            FAILED(SwapChain->GetBuffer(
                Index,
                IID_PPV_ARGS(Frame.BackBuffer.ReleaseAndGetAddressOf()))))
        {
            ReleaseImGuiResourcesLocked();
            return false;
        }

        Frame.RenderTarget = RenderTarget;
        Device->CreateRenderTargetView(Frame.BackBuffer.Get(), nullptr, RenderTarget);
        RenderTarget.ptr += RenderTargetIncrement;
    }

    if (FAILED(Device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            g_Dx12Frames.front().Allocator.Get(),
            nullptr,
            IID_PPV_ARGS(g_ImGuiCommandList.ReleaseAndGetAddressOf()))) ||
        FAILED(g_ImGuiCommandList->Close()) ||
        FAILED(Device->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(g_ImGuiFence.ReleaseAndGetAddressOf()))))
    {
        ReleaseImGuiResourcesLocked();
        return false;
    }

    g_ImGuiFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_ImGuiFenceEvent)
    {
        ReleaseImGuiResourcesLocked();
        return false;
    }

    g_ShaderDescriptors.Heap = g_ShaderDescriptorHeap.Get();
    g_ShaderDescriptors.Increment = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_ShaderDescriptors.CpuStart = g_ShaderDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    g_ShaderDescriptors.GpuStart = g_ShaderDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    g_ShaderDescriptors.FreeIndices.reserve(64);
    for (UINT Index = 64; Index > 0; --Index)
        g_ShaderDescriptors.FreeIndices.push_back(Index - 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.ConfigWindowsMoveFromTitleBarOnly = false;
    Io.IniFilename = nullptr;
    ImFont* BodyFont = Io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0F);
    if (!BodyFont)
        BodyFont = Io.Fonts->AddFontDefault();
    Io.FontDefault = BodyFont;
    g_TitleFont = Io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 25.0F);
    if (!g_TitleFont)
        g_TitleFont = BodyFont;
    ApplyImGuiStyle();

    if (!ImGui_ImplWin32_Init(g_GameWindow))
    {
        ReleaseImGuiResourcesLocked();
        return false;
    }
    g_Win32BackendReady = true;

    ImGui_ImplDX12_InitInfo Initialization{};
    Initialization.Device = Device.Get();
    Initialization.CommandQueue = Queue.Get();
    Initialization.NumFramesInFlight = static_cast<int>(SwapChainDescription.BufferCount);
    Initialization.RTVFormat = SwapChainDescription.BufferDesc.Format;
    Initialization.DSVFormat = DXGI_FORMAT_UNKNOWN;
    Initialization.UserData = &g_ShaderDescriptors;
    Initialization.SrvDescriptorHeap = g_ShaderDescriptorHeap.Get();
    Initialization.SrvDescriptorAllocFn = AllocateShaderDescriptor;
    Initialization.SrvDescriptorFreeFn = FreeShaderDescriptor;
    if (!ImGui_ImplDX12_Init(&Initialization))
    {
        ReleaseImGuiResourcesLocked();
        return false;
    }
    g_Dx12BackendReady = true;

    SetLastError(0);
    const LONG_PTR Original = SetWindowLongPtrW(
        g_GameWindow,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&HookedWindowProcedure));
    if (Original == 0 && GetLastError() != 0)
    {
        ReleaseImGuiResourcesLocked();
        return false;
    }
    g_OriginalWindowProc = reinterpret_cast<WNDPROC>(Original);
    g_ImGuiReady.store(true, std::memory_order_release);
    WriteDiagnosticLog(
        "ImGui initialized: window=%p device=%p queue=%p buffers=%u format=%u "
        "queue_source=%s present_thread_candidate=%p first_candidate=%p",
        g_GameWindow,
        Device.Get(),
        Queue.Get(),
        SwapChainDescription.BufferCount,
        static_cast<unsigned>(SwapChainDescription.BufferDesc.Format),
        QueueSource,
        PresentThreadQueue.Get(),
        FirstQueue.Get());
    return true;
}

bool IsTrackedSwapChain(IDXGISwapChain* SwapChain)
{
    if (!SwapChain || !g_ImGuiSwapChain)
        return false;

    Microsoft::WRL::ComPtr<IDXGISwapChain3> Candidate;
    return SUCCEEDED(SwapChain->QueryInterface(IID_PPV_ARGS(Candidate.ReleaseAndGetAddressOf()))) &&
           Candidate.Get() == g_ImGuiSwapChain.Get();
}

bool RefreshRenderQueueLocked()
{
    if (!g_RenderQueue || !g_ImGuiDevice)
        return false;

    Microsoft::WRL::ComPtr<ID3D12Device> QueueDevice;
    if (FAILED(g_RenderQueue->GetDevice(IID_PPV_ARGS(QueueDevice.ReleaseAndGetAddressOf()))) ||
        QueueDevice.Get() != g_ImGuiDevice.Get())
    {
        return false;
    }

    // A DXGI swap chain is permanently associated with the command queue used
    // when it was created. Other Unreal subsystems may submit work through
    // additional direct queues later. Never migrate ImGui to those queues:
    // doing so loses ordering with Present and can fault asynchronously in the
    // graphics driver.
    return true;
}

void HandleImGuiHotkeys()
{
    int MenuKey{};
    int NextPageKey{};
    {
        std::scoped_lock SettingsLock(g_SettingsMutex);
        MenuKey = g_Settings.MenuHotkey;
        NextPageKey = g_Settings.NextPageHotkey;
    }
    if ((GetAsyncKeyState(MenuKey) & 1) != 0)
        g_MenuOpen.store(!g_MenuOpen.load(std::memory_order_relaxed), std::memory_order_relaxed);

    if (g_MenuOpen.load(std::memory_order_relaxed) && (GetAsyncKeyState(NextPageKey) & 1) != 0)
    {
        std::scoped_lock SettingsLock(g_SettingsMutex);
        const int Next = (static_cast<int>(g_Settings.ActiveTab) + 1) % static_cast<int>(MenuTab::Count);
        g_Settings.ActiveTab = static_cast<MenuTab>(Next);
    }
}

bool SubmitImGuiDrawData(IDXGISwapChain* SwapChain)
{
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ImGuiSubmit);
    if (!SwapChain || !g_ImGuiFence || !g_ImGuiCommandList || !g_RenderQueue ||
        !g_ShaderDescriptorHeap || g_Dx12Frames.empty() || !ImGui::GetCurrentContext())
    {
        WriteDiagnosticLog("ImGui submit rejected: one or more render resources are missing");
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain3> SwapChain3;
    if (FAILED(SwapChain->QueryInterface(IID_PPV_ARGS(SwapChain3.ReleaseAndGetAddressOf()))))
    {
        WriteDiagnosticLog("ImGui submit failed: swap-chain QueryInterface");
        return false;
    }

    const UINT BackBufferIndex = SwapChain3->GetCurrentBackBufferIndex();
    if (BackBufferIndex >= g_Dx12Frames.size())
    {
        WriteDiagnosticLog(
            "ImGui submit failed: back-buffer index %u exceeds frame count %llu",
            BackBufferIndex,
            static_cast<unsigned long long>(g_Dx12Frames.size()));
        return false;
    }

    Dx12FrameContext& Frame = g_Dx12Frames[BackBufferIndex];
    if (Frame.FenceValue != 0 && g_ImGuiFence->GetCompletedValue() < Frame.FenceValue)
    {
        if (FAILED(g_ImGuiFence->SetEventOnCompletion(Frame.FenceValue, g_ImGuiFenceEvent)) ||
            WaitForSingleObject(g_ImGuiFenceEvent, 3000) != WAIT_OBJECT_0)
        {
            WriteDiagnosticLog(
                "ImGui submit failed: timed out waiting for fence %llu",
                static_cast<unsigned long long>(Frame.FenceValue));
            return false;
        }
    }

    if (FAILED(Frame.Allocator->Reset()) ||
        FAILED(g_ImGuiCommandList->Reset(Frame.Allocator.Get(), nullptr)))
    {
        WriteDiagnosticLog("ImGui submit failed: allocator or command-list reset");
        return false;
    }

    D3D12_RESOURCE_BARRIER Barrier{};
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Frame.BackBuffer.Get();
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_ImGuiCommandList->ResourceBarrier(1, &Barrier);
    g_ImGuiCommandList->OMSetRenderTargets(1, &Frame.RenderTarget, FALSE, nullptr);
    ID3D12DescriptorHeap* DescriptorHeaps[]{g_ShaderDescriptorHeap.Get()};
    g_ImGuiCommandList->SetDescriptorHeaps(1, DescriptorHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_ImGuiCommandList.Get());
    std::swap(Barrier.Transition.StateBefore, Barrier.Transition.StateAfter);
    g_ImGuiCommandList->ResourceBarrier(1, &Barrier);
    if (FAILED(g_ImGuiCommandList->Close()))
    {
        WriteDiagnosticLog("ImGui submit failed: command-list close");
        return false;
    }

    ID3D12CommandList* CommandLists[]{g_ImGuiCommandList.Get()};
    struct ImGuiSubmissionGuard
    {
        ImGuiSubmissionGuard() { g_IsImGuiSubmission = true; }
        ~ImGuiSubmissionGuard() { g_IsImGuiSubmission = false; }
    };
    {
        const ImGuiSubmissionGuard SubmissionGuard{};
        g_RenderQueue->ExecuteCommandLists(1, CommandLists);
    }
    const UINT64 FenceValue = ++g_LastFenceValue;
    if (FAILED(g_RenderQueue->Signal(g_ImGuiFence.Get(), FenceValue)))
    {
        WriteDiagnosticLog("ImGui submit failed: queue fence signal");
        return false;
    }

    Frame.FenceValue = FenceValue;
    return true;
}

bool RenderImGuiFrame(IDXGISwapChain* SwapChain)
{
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ImGuiFrame);
    HandleImGuiHotkeys();
    const bool Visible =
        g_MenuOpen.load(std::memory_order_relaxed) &&
        g_OverlayEnabled.load(std::memory_order_relaxed) &&
        g_OfflineAllowed.load(std::memory_order_relaxed);
    if (!Visible)
    {
        if (ImGui::GetCurrentContext())
            ImGui::GetIO().MouseDrawCursor = false;
        return true;
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = true;
    if (!g_FirstImGuiFrameLogged.exchange(true, std::memory_order_acq_rel))
        WriteDiagnosticLog("First visible ImGui frame started");
    {
        std::scoped_lock SettingsLock(g_SettingsMutex);
        RenderImGuiSettingsMenu();
    }
    ImGui::Render();
    const bool Submitted = SubmitImGuiDrawData(SwapChain);
    if (Submitted)
    {
        const std::uint64_t FrameNumber = g_ImGuiFrameCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        if (FrameNumber <= 3 || (FrameNumber % 300) == 0)
        {
            WriteDiagnosticLog(
                "ImGui frame=%llu submitted fence=%llu queue=%p device=%p",
                static_cast<unsigned long long>(FrameNumber),
                static_cast<unsigned long long>(g_LastFenceValue),
                g_RenderQueue.Get(),
                g_ImGuiDevice.Get());
        }
    }
    return Submitted;
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* SwapChain, UINT SyncInterval, UINT Flags)
{
    const ActiveHookCall ActiveCall{};
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ImGuiPresent);
    if (g_Running.load(std::memory_order_relaxed) &&
        g_OverlayEnabled.load(std::memory_order_relaxed) &&
        g_OfflineAllowed.load(std::memory_order_relaxed) &&
        !g_ImGuiFailed.load(std::memory_order_acquire))
    {
        std::scoped_lock Lock(g_ImGuiMutex);
        if (!g_ImGuiReady.load(std::memory_order_acquire) &&
            !InitializeImGuiForSwapChain(SwapChain) &&
            !g_ImGuiInitFailureLogged.exchange(true, std::memory_order_acq_rel))
        {
            WriteDiagnosticLog("ImGui initialization attempt failed; retrying on later Present calls");
        }
        if (g_ImGuiReady.load(std::memory_order_acquire) &&
            IsTrackedSwapChain(SwapChain))
        {
            if (!g_ImGuiDevice)
                SuspendImGuiLocked("tracked swap chain has no device");
            else if (const HRESULT RemovedReason = g_ImGuiDevice->GetDeviceRemovedReason(); FAILED(RemovedReason))
            {
                WriteDiagnosticLog("DX12 device removed: HRESULT=0x%08lX", static_cast<unsigned long>(RemovedReason));
                SuspendImGuiLocked("DX12 device removed");
            }
            else if (!RefreshRenderQueueLocked())
                SuspendImGuiLocked("fixed presentation queue validation failed");
            else if (!RenderImGuiFrame(SwapChain))
                SuspendImGuiLocked("ImGui frame submission failed");
        }
    }

    return g_OriginalPresent
        ? g_OriginalPresent(SwapChain, SyncInterval, Flags)
        : DXGI_ERROR_INVALID_CALL;
}

HRESULT STDMETHODCALLTYPE HookedResizeBuffers(
    IDXGISwapChain* SwapChain,
    UINT BufferCount,
    UINT Width,
    UINT Height,
    DXGI_FORMAT NewFormat,
    UINT SwapChainFlags)
{
    const ActiveHookCall ActiveCall{};
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ImGuiResize);
    WriteDiagnosticLog(
        "ResizeBuffers: buffers=%u size=%ux%u format=%u flags=0x%X",
        BufferCount,
        Width,
        Height,
        static_cast<unsigned>(NewFormat),
        SwapChainFlags);
    {
        std::scoped_lock Lock(g_ImGuiMutex);
        if (IsTrackedSwapChain(SwapChain) &&
            (ImGui::GetCurrentContext() || g_ImGuiDevice))
        {
            ReleaseImGuiResourcesLocked();
        }
    }

    return g_OriginalResizeBuffers
        ? g_OriginalResizeBuffers(SwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags)
        : DXGI_ERROR_INVALID_CALL;
}

void STDMETHODCALLTYPE HookedExecuteCommandLists(
    ID3D12CommandQueue* Queue,
    UINT Count,
    ID3D12CommandList* const* CommandLists)
{
    const ActiveHookCall ActiveCall{};
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::DxExecute);
    ID3D12CommandQueue* PreviousQueue = nullptr;
    bool QueueChanged = false;
    if (!g_IsImGuiSubmission && Queue &&
        Queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        g_ThreadDirectQueue = Queue;
        std::scoped_lock Lock(g_QueueMutex);
        if (!g_FirstDirectQueue)
            g_FirstDirectQueue = Queue;
        if (g_LatestDirectQueue.Get() != Queue)
        {
            PreviousQueue = g_LatestDirectQueue.Get();
            g_LatestDirectQueue = Queue;
            QueueChanged = true;
        }
    }

    if (g_OriginalExecuteCommandLists)
        g_OriginalExecuteCommandLists(Queue, Count, CommandLists);

    if (QueueChanged)
    {
        const std::uint32_t ChangeNumber =
            g_DirectQueueChangeCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (ChangeNumber <= 16)
        {
            WriteDiagnosticLog(
                "Direct queue observation #%lu: previous=%p current=%p lists=%u",
                static_cast<unsigned long>(ChangeNumber),
                PreviousQueue,
                Queue,
                Count);
        }
    }
}

bool GetDx12HookTargets(void** Present, void** ResizeBuffers, void** ExecuteCommandLists)
{
    if (!Present || !ResizeBuffers || !ExecuteCommandLists)
        return false;

    constexpr wchar_t ClassName[] = L"NMRiH2OverlayDx12Probe";
    HINSTANCE Instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW WindowClass{};
    WindowClass.cbSize = sizeof(WindowClass);
    WindowClass.style = CS_CLASSDC;
    WindowClass.lpfnWndProc = DefWindowProcW;
    WindowClass.hInstance = Instance;
    WindowClass.lpszClassName = ClassName;
    const ATOM ClassAtom = RegisterClassExW(&WindowClass);
    if (!ClassAtom)
        return false;

    HWND Window = CreateWindowExW(
        0,
        ClassName,
        L"NMRiH2 DX12 probe",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        64,
        64,
        nullptr,
        nullptr,
        Instance,
        nullptr);

    bool Success = false;
    Microsoft::WRL::ComPtr<IDXGIFactory4> Factory;
    Microsoft::WRL::ComPtr<ID3D12Device> Device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> Queue;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> SwapChain1;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> SwapChain3;

    if (Window)
    {
        do
        {
            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(Factory.ReleaseAndGetAddressOf()))) ||
                FAILED(D3D12CreateDevice(
                    nullptr,
                    D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(Device.ReleaseAndGetAddressOf()))))
            {
                break;
            }

            D3D12_COMMAND_QUEUE_DESC QueueDescription{};
            QueueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            if (FAILED(Device->CreateCommandQueue(
                    &QueueDescription,
                    IID_PPV_ARGS(Queue.ReleaseAndGetAddressOf()))))
            {
                break;
            }

            DXGI_SWAP_CHAIN_DESC1 SwapChainDescription{};
            SwapChainDescription.Width = 64;
            SwapChainDescription.Height = 64;
            SwapChainDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            SwapChainDescription.SampleDesc.Count = 1;
            SwapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            SwapChainDescription.BufferCount = 2;
            SwapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (FAILED(Factory->CreateSwapChainForHwnd(
                    Queue.Get(),
                    Window,
                    &SwapChainDescription,
                    nullptr,
                    nullptr,
                    SwapChain1.ReleaseAndGetAddressOf())) ||
                FAILED(SwapChain1.As(&SwapChain3)))
            {
                break;
            }

            void** SwapChainVTable = *reinterpret_cast<void***>(SwapChain3.Get());
            void** QueueVTable = *reinterpret_cast<void***>(Queue.Get());
            if (!SwapChainVTable || !QueueVTable)
                break;

            *Present = SwapChainVTable[8];
            *ResizeBuffers = SwapChainVTable[13];
            *ExecuteCommandLists = QueueVTable[10];
            Success = *Present && *ResizeBuffers && *ExecuteCommandLists;
        }
        while (false);
    }

    SwapChain3.Reset();
    SwapChain1.Reset();
    Queue.Reset();
    Device.Reset();
    Factory.Reset();
    if (Window)
        DestroyWindow(Window);
    UnregisterClassW(ClassName, Instance);
    return Success;
}

bool InstallDx12Hooks()
{
    if (!GetDx12HookTargets(
            &g_PresentTarget,
            &g_ResizeBuffersTarget,
            &g_ExecuteCommandListsTarget))
    {
        return false;
    }

    const MH_STATUS InitializeStatus = MH_Initialize();
    if (InitializeStatus != MH_OK && InitializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        return false;

    const bool Created =
        MH_CreateHook(
            g_PresentTarget,
            reinterpret_cast<void*>(&HookedPresent),
            reinterpret_cast<void**>(&g_OriginalPresent)) == MH_OK &&
        MH_CreateHook(
            g_ResizeBuffersTarget,
            reinterpret_cast<void*>(&HookedResizeBuffers),
            reinterpret_cast<void**>(&g_OriginalResizeBuffers)) == MH_OK &&
        MH_CreateHook(
            g_ExecuteCommandListsTarget,
            reinterpret_cast<void*>(&HookedExecuteCommandLists),
            reinterpret_cast<void**>(&g_OriginalExecuteCommandLists)) == MH_OK;

    if (!Created ||
        MH_EnableHook(g_PresentTarget) != MH_OK ||
        MH_EnableHook(g_ResizeBuffersTarget) != MH_OK ||
        MH_EnableHook(g_ExecuteCommandListsTarget) != MH_OK)
    {
        if (g_PresentTarget)
            MH_RemoveHook(g_PresentTarget);
        if (g_ResizeBuffersTarget)
            MH_RemoveHook(g_ResizeBuffersTarget);
        if (g_ExecuteCommandListsTarget)
            MH_RemoveHook(g_ExecuteCommandListsTarget);
        MH_Uninitialize();
        return false;
    }

    g_DxHooksInstalled.store(true, std::memory_order_release);
    return true;
}

void DisableDx12Hooks()
{
    g_DxHooksInstalled.store(false, std::memory_order_release);
    if (g_PresentTarget)
        MH_DisableHook(g_PresentTarget);
    if (g_ResizeBuffersTarget)
        MH_DisableHook(g_ResizeBuffersTarget);
    if (g_ExecuteCommandListsTarget)
        MH_DisableHook(g_ExecuteCommandListsTarget);

    std::scoped_lock Lock(g_ImGuiMutex);
    g_ImGuiReady.store(false, std::memory_order_release);
    RestoreWindowProcedureLocked();
}

void FinalizeDx12Hooks()
{
    {
        std::scoped_lock Lock(g_ImGuiMutex);
        ReleaseImGuiResourcesLocked();
    }

    if (g_PresentTarget)
        MH_RemoveHook(g_PresentTarget);
    if (g_ResizeBuffersTarget)
        MH_RemoveHook(g_ResizeBuffersTarget);
    if (g_ExecuteCommandListsTarget)
        MH_RemoveHook(g_ExecuteCommandListsTarget);
    MH_Uninitialize();

    {
        std::scoped_lock QueueLock(g_QueueMutex);
        g_FirstDirectQueue.Reset();
        g_LatestDirectQueue.Reset();
    }
    g_OriginalPresent = nullptr;
    g_OriginalResizeBuffers = nullptr;
    g_OriginalExecuteCommandLists = nullptr;
    g_PresentTarget = nullptr;
    g_ResizeBuffersTarget = nullptr;
    g_ExecuteCommandListsTarget = nullptr;
}
#else
bool InstallDx12Hooks()
{
    return false;
}

void DisableDx12Hooks()
{
}

void FinalizeDx12Hooks()
{
}
#endif

#ifndef OVERLAY_SOURCE_CHECK
struct GameWindowSearch
{
    DWORD ProcessId{};
    HWND BestWindow{};
    long long BestArea{};
};

BOOL CALLBACK FindGameWindowCallback(HWND Window, LPARAM Parameter)
{
    auto* Search = reinterpret_cast<GameWindowSearch*>(Parameter);
    if (!Search || !IsWindowVisible(Window) || GetWindow(Window, GW_OWNER))
        return TRUE;

    DWORD WindowProcessId{};
    GetWindowThreadProcessId(Window, &WindowProcessId);
    if (WindowProcessId != Search->ProcessId)
        return TRUE;

    RECT Client{};
    if (!GetClientRect(Window, &Client))
        return TRUE;

    const long long Width = static_cast<long long>(Client.right - Client.left);
    const long long Height = static_cast<long long>(Client.bottom - Client.top);
    const long long Area = Width * Height;
    if (Width >= 640 && Height >= 360 && Area > Search->BestArea)
    {
        Search->BestWindow = Window;
        Search->BestArea = Area;
    }
    return TRUE;
}

HWND FindCurrentProcessGameWindow()
{
    GameWindowSearch Search{GetCurrentProcessId()};
    EnumWindows(&FindGameWindowCallback, reinterpret_cast<LPARAM>(&Search));
    return Search.BestWindow;
}

LRESULT CALLBACK ExternalOverlayWindowProcedure(
    HWND Window,
    UINT Message,
    WPARAM WParam,
    LPARAM LParam)
{
    if (ImGui::GetCurrentContext() &&
        ImGui_ImplWin32_WndProcHandler(Window, Message, WParam, LParam))
    {
        return 1;
    }

    if (Message == WM_ERASEBKGND)
        return 1;
    if (Message == WM_NCHITTEST)
        return HTCLIENT;
    if (Message == WM_MOUSEACTIVATE)
        return MA_ACTIVATE;
    if (Message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

bool CreateExternalRenderTarget()
{
    if (!g_ExternalSwapChain || !g_ExternalDevice)
        return false;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> BackBuffer;
    if (FAILED(g_ExternalSwapChain->GetBuffer(
            0,
            IID_PPV_ARGS(BackBuffer.ReleaseAndGetAddressOf()))) ||
        FAILED(g_ExternalDevice->CreateRenderTargetView(
            BackBuffer.Get(),
            nullptr,
            g_ExternalRenderTarget.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    return true;
}

bool ResizeExternalSwapChain(UINT Width, UINT Height)
{
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ExternalUiResize);
    if (!g_ExternalSwapChain || !g_ExternalDeviceContext || Width == 0 || Height == 0)
        return false;
    if (Width == g_ExternalWidth && Height == g_ExternalHeight && g_ExternalRenderTarget)
        return true;

    g_ExternalDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    g_ExternalRenderTarget.Reset();
    if (FAILED(g_ExternalSwapChain->ResizeBuffers(
            0,
            Width,
            Height,
            DXGI_FORMAT_UNKNOWN,
            0)))
    {
        WriteDiagnosticLog("External ImGui resize failed: %ux%u", Width, Height);
        return false;
    }

    g_ExternalWidth = Width;
    g_ExternalHeight = Height;
    return CreateExternalRenderTarget();
}

bool UpdateExternalWindowBounds()
{
    if (!g_TargetGameWindow || !IsWindow(g_TargetGameWindow) ||
        !IsWindowVisible(g_TargetGameWindow) || IsIconic(g_TargetGameWindow))
    {
        return false;
    }

    RECT Client{};
    POINT Origin{};
    if (!GetClientRect(g_TargetGameWindow, &Client) ||
        !ClientToScreen(g_TargetGameWindow, &Origin))
    {
        return false;
    }

    const LONG WidthValue = Client.right - Client.left;
    const LONG HeightValue = Client.bottom - Client.top;
    if (WidthValue <= 0 || HeightValue <= 0)
        return false;

    const UINT Width = static_cast<UINT>(WidthValue);
    const UINT Height = static_cast<UINT>(HeightValue);
    const bool BoundsChanged =
        !g_ExternalBoundsReady ||
        g_ExternalLeft != Origin.x ||
        g_ExternalTop != Origin.y ||
        g_ExternalWidth != Width ||
        g_ExternalHeight != Height;

    if (!BoundsChanged)
        return static_cast<bool>(g_ExternalRenderTarget);

    if (!SetWindowPos(
            g_ExternalOverlayWindow,
            HWND_TOPMOST,
            Origin.x,
            Origin.y,
            WidthValue,
            HeightValue,
            SWP_NOACTIVATE))
    {
        return false;
    }

    if (!ResizeExternalSwapChain(Width, Height))
        return false;

    g_ExternalLeft = Origin.x;
    g_ExternalTop = Origin.y;
    g_ExternalBoundsReady = true;
    return true;
}

void AcquireExternalMenuInput()
{
    if (!g_ExternalOverlayWindow)
        return;

    ShowWindow(g_ExternalOverlayWindow, SW_SHOWNOACTIVATE);
    if (GetForegroundWindow() != g_ExternalOverlayWindow)
        SetForegroundWindow(g_ExternalOverlayWindow);
    if (GetActiveWindow() != g_ExternalOverlayWindow)
        SetActiveWindow(g_ExternalOverlayWindow);
    if (GetFocus() != g_ExternalOverlayWindow)
        SetFocus(g_ExternalOverlayWindow);
    if (GetCapture() != g_ExternalOverlayWindow)
        SetCapture(g_ExternalOverlayWindow);
    ClipCursor(nullptr);
}

void ReleaseExternalMenuInput(bool RestoreGameFocus)
{
    if (GetCapture() == g_ExternalOverlayWindow)
        ReleaseCapture();
    if (g_ExternalOverlayWindow)
        ShowWindow(g_ExternalOverlayWindow, SW_HIDE);
    ClipCursor(nullptr);
    if (RestoreGameFocus && g_TargetGameWindow)
    {
        SetForegroundWindow(g_TargetGameWindow);
        SetActiveWindow(g_TargetGameWindow);
        SetFocus(g_TargetGameWindow);
    }
}

void FeedExternalMouseStateToImGui()
{
    POINT Cursor{};
    if (GetCursorPos(&Cursor) &&
        ScreenToClient(g_ExternalOverlayWindow, &Cursor))
    {
        ImGui::GetIO().AddMousePosEvent(
            static_cast<float>(Cursor.x),
            static_cast<float>(Cursor.y));
    }

    ImGuiIO& Io = ImGui::GetIO();
    Io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    Io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    Io.AddMouseButtonEvent(2, (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0);
}

bool InitializeExternalImGuiWindow()
{
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ExternalUiStart);
    g_TargetGameWindow = FindCurrentProcessGameWindow();
    if (!g_TargetGameWindow)
    {
        WriteDiagnosticLog("External ImGui could not find the game window");
        return false;
    }

    constexpr wchar_t WindowClassName[] = L"NMRiH2ExternalImGuiOverlay";
    WNDCLASSEXW WindowClass{};
    WindowClass.cbSize = sizeof(WindowClass);
    WindowClass.style = CS_CLASSDC;
    WindowClass.lpfnWndProc = &ExternalOverlayWindowProcedure;
    WindowClass.hInstance = GetModuleHandleW(nullptr);
    WindowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    WindowClass.lpszClassName = WindowClassName;
    if (!RegisterClassExW(&WindowClass))
    {
        WriteDiagnosticLog("External ImGui window-class registration failed");
        return false;
    }

    g_ExternalOverlayWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        WindowClassName,
        L"NMRiH2 Offline Inspector",
        WS_POPUP,
        0,
        0,
        1280,
        720,
        nullptr,
        nullptr,
        WindowClass.hInstance,
        nullptr);
    if (!g_ExternalOverlayWindow)
    {
        UnregisterClassW(WindowClassName, WindowClass.hInstance);
        WriteDiagnosticLog("External ImGui window creation failed");
        return false;
    }

    SetLayeredWindowAttributes(g_ExternalOverlayWindow, RGB(0, 0, 0), 0, LWA_COLORKEY);

    DXGI_SWAP_CHAIN_DESC SwapChainDescription{};
    SwapChainDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    SwapChainDescription.SampleDesc.Count = 1;
    SwapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    SwapChainDescription.BufferCount = 2;
    SwapChainDescription.OutputWindow = g_ExternalOverlayWindow;
    SwapChainDescription.Windowed = TRUE;
    SwapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL FeatureLevel{};
    constexpr D3D_FEATURE_LEVEL RequestedLevels[]{D3D_FEATURE_LEVEL_11_0};
    if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            RequestedLevels,
            static_cast<UINT>(std::size(RequestedLevels)),
            D3D11_SDK_VERSION,
            &SwapChainDescription,
            g_ExternalSwapChain.ReleaseAndGetAddressOf(),
            g_ExternalDevice.ReleaseAndGetAddressOf(),
            &FeatureLevel,
            g_ExternalDeviceContext.ReleaseAndGetAddressOf())) ||
        !CreateExternalRenderTarget())
    {
        WriteDiagnosticLog("External ImGui D3D11 initialization failed");
        DestroyWindow(g_ExternalOverlayWindow);
        g_ExternalOverlayWindow = nullptr;
        UnregisterClassW(WindowClassName, WindowClass.hInstance);
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& Io = ImGui::GetIO();
    Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Io.ConfigWindowsMoveFromTitleBarOnly = false;
    Io.IniFilename = nullptr;
    ImFont* BodyFont = Io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0F);
    if (!BodyFont)
        BodyFont = Io.Fonts->AddFontDefault();
    Io.FontDefault = BodyFont;
    g_TitleFont = Io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguisb.ttf", 25.0F);
    if (!g_TitleFont)
        g_TitleFont = BodyFont;
    ApplyImGuiStyle();

    const bool Win32Initialized = ImGui_ImplWin32_Init(g_ExternalOverlayWindow);
    const bool Dx11Initialized = Win32Initialized &&
        ImGui_ImplDX11_Init(g_ExternalDevice.Get(), g_ExternalDeviceContext.Get());
    if (!Win32Initialized || !Dx11Initialized)
    {
        WriteDiagnosticLog("External ImGui backend initialization failed");
        if (Dx11Initialized)
            ImGui_ImplDX11_Shutdown();
        if (Win32Initialized)
            ImGui_ImplWin32_Shutdown();
        if (ImGui::GetCurrentContext())
            ImGui::DestroyContext();
        DestroyWindow(g_ExternalOverlayWindow);
        g_ExternalOverlayWindow = nullptr;
        UnregisterClassW(WindowClassName, WindowClass.hInstance);
        return false;
    }

    g_ImGuiReady.store(true, std::memory_order_release);
    WriteDiagnosticLog(
        "External ImGui initialized: game_window=%p overlay_window=%p device=%p",
        g_TargetGameWindow,
        g_ExternalOverlayWindow,
        g_ExternalDevice.Get());
    return true;
}

void ShutdownExternalImGuiWindow()
{
    constexpr wchar_t WindowClassName[] = L"NMRiH2ExternalImGuiOverlay";
    g_ImGuiReady.store(false, std::memory_order_release);
    ReleaseExternalMenuInput(false);
    if (ImGui::GetCurrentContext())
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    g_TitleFont = nullptr;
    g_MenuPlacementInitialized = false;
    g_ExternalRenderTarget.Reset();
    g_ExternalSwapChain.Reset();
    g_ExternalDeviceContext.Reset();
    g_ExternalDevice.Reset();
    if (g_ExternalOverlayWindow)
    {
        DestroyWindow(g_ExternalOverlayWindow);
        g_ExternalOverlayWindow = nullptr;
    }
    UnregisterClassW(WindowClassName, GetModuleHandleW(nullptr));
    g_TargetGameWindow = nullptr;
    g_ExternalWidth = 0;
    g_ExternalHeight = 0;
    g_ExternalLeft = 0;
    g_ExternalTop = 0;
    g_ExternalBoundsReady = false;
}

DWORD WINAPI ExternalImGuiWorker(void*)
{
    if (!InitializeExternalImGuiWindow())
    {
        g_ImGuiFailed.store(true, std::memory_order_release);
        ShutdownExternalImGuiWindow();
        return 1;
    }

    bool WasVisible = false;
    MSG Message{};
    while (g_Running.load(std::memory_order_relaxed))
    {
        while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
            if (Message.message == WM_QUIT)
                g_Running.store(false, std::memory_order_relaxed);
        }

        HandleImGuiHotkeys();
        const HWND ForegroundWindow = GetForegroundWindow();
        const bool MenuVisible =
            g_MenuOpen.load(std::memory_order_relaxed) &&
            g_OverlayEnabled.load(std::memory_order_relaxed) &&
            g_OfflineAllowed.load(std::memory_order_relaxed) &&
            (ForegroundWindow == g_TargetGameWindow ||
             ForegroundWindow == g_ExternalOverlayWindow);

        if (!MenuVisible || !UpdateExternalWindowBounds())
        {
            if (WasVisible)
            {
                ReleaseExternalMenuInput(ForegroundWindow == g_ExternalOverlayWindow);
                WriteDiagnosticLog("External ImGui released mouse capture");
            }
            WasVisible = false;
            Sleep(16);
            continue;
        }

        if (!WasVisible)
        {
            AcquireExternalMenuInput();
            WriteDiagnosticLog("External ImGui acquired focus and mouse capture");
        }
        WasVisible = true;
        const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ExternalUiFrame);
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        FeedExternalMouseStateToImGui();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = true;
        {
            std::scoped_lock SettingsLock(g_SettingsMutex);
            RenderImGuiSettingsMenu();
        }
        ImGui::Render();

        constexpr float ClearColor[]{0.0F, 0.0F, 0.0F, 0.0F};
        ID3D11RenderTargetView* RenderTarget = g_ExternalRenderTarget.Get();
        g_ExternalDeviceContext->OMSetRenderTargets(1, &RenderTarget, nullptr);
        g_ExternalDeviceContext->ClearRenderTargetView(RenderTarget, ClearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const HRESULT PresentResult = g_ExternalSwapChain->Present(1, 0);
        if (FAILED(PresentResult))
        {
            WriteDiagnosticLog(
                "External ImGui Present failed: HRESULT=0x%08lX",
                static_cast<unsigned long>(PresentResult));
            break;
        }

        const std::uint64_t FrameNumber =
            g_ImGuiFrameCounter.fetch_add(1, std::memory_order_relaxed) + 1;
        if (FrameNumber <= 3 || (FrameNumber % 300) == 0)
        {
            WriteDiagnosticLog(
                "External ImGui frame=%llu submitted on private D3D11 device",
                static_cast<unsigned long long>(FrameNumber));
        }
    }

    ShutdownExternalImGuiWindow();
    WriteDiagnosticLog("External ImGui stopped");
    return 0;
}

bool StartExternalImGuiWindow()
{
    g_ExternalUiThread = CreateThread(
        nullptr,
        0,
        &ExternalImGuiWorker,
        nullptr,
        0,
        nullptr);
    return g_ExternalUiThread != nullptr;
}

bool StopExternalImGuiWindow()
{
    if (!g_ExternalUiThread)
        return true;
    const DWORD WaitResult = WaitForSingleObject(g_ExternalUiThread, 5000);
    CloseHandle(g_ExternalUiThread);
    g_ExternalUiThread = nullptr;
    return WaitResult == WAIT_OBJECT_0;
}
#else
bool StartExternalImGuiWindow()
{
    return false;
}

bool StopExternalImGuiWindow()
{
    return true;
}
#endif

FLinearColor MakeColor(float R, float G, float B, float A = 1.0F)
{
    return FLinearColor{R, G, B, A};
}

bool IsUsableObject(const UObject* Object)
{
    if (!Object || !Object->VTable || !Object->Class || Object->Index < 0)
        return false;

    const std::uint32_t Flags = static_cast<std::uint32_t>(Object->Flags);
    constexpr std::uint32_t InvalidFlags =
        static_cast<std::uint32_t>(EObjectFlags::BeginDestroyed) |
        static_cast<std::uint32_t>(EObjectFlags::FinishDestroyed) |
        static_cast<std::uint32_t>(EObjectFlags::MirroredGarbage);
    return (Flags & InvalidFlags) == 0;
}

APlayerController* GetLocalController(UWorld* World);

std::array<FGameplayAttributeData*, 6> GetDamageAttributes(UKAttributeSet_Global* AttributeSet)
{
    if (!AttributeSet)
        return {};

    return {
        &AttributeSet->PlayerOutgoingStabilityDamageMultiplier,
        &AttributeSet->PlayerOutgoingMeleeDamageMultiplier,
        &AttributeSet->PlayerOutgoingMeleeLimbDamageMultiplier,
        &AttributeSet->PlayerOutgoingFirearmHeadDamageMultiplier,
        &AttributeSet->PlayerOutgoingFirearmBodyDamageMultiplier,
        &AttributeSet->PlayerOutgoingFirearmLimbDamageMultiplier
    };
}

void NotifyDamageAttributeChanged(
    UKAttributeSet_Global* AttributeSet,
    std::size_t Index,
    const FGameplayAttributeData& OldValue)
{
    if (!AttributeSet)
        return;

    switch (Index)
    {
    case 0: AttributeSet->OnRep_PlayerOutgoingStabilityDamageMultiplier(OldValue); break;
    case 1: AttributeSet->OnRep_PlayerOutgoingMeleeDamageMultiplier(OldValue); break;
    case 2: AttributeSet->OnRep_PlayerOutgoingMeleeLimbDamageMultiplier(OldValue); break;
    case 3: AttributeSet->OnRep_PlayerOutgoingFirearmHeadDamageMultiplier(OldValue); break;
    case 4: AttributeSet->OnRep_PlayerOutgoingFirearmBodyDamageMultiplier(OldValue); break;
    case 5: AttributeSet->OnRep_PlayerOutgoingFirearmLimbDamageMultiplier(OldValue); break;
    default: break;
    }
}

bool IsSaneDamageAttribute(const FGameplayAttributeData* Attribute)
{
    return Attribute &&
           std::isfinite(Attribute->BaseValue) &&
           std::isfinite(Attribute->CurrentValue) &&
           Attribute->BaseValue > 0.0F && Attribute->BaseValue < 1000.0F &&
           Attribute->CurrentValue > 0.0F && Attribute->CurrentValue < 1000.0F;
}

UKAttributeSet_Global* FindGlobalDamageAttributeSet(APawn* LocalPawn)
{
    UClass* CharacterClass = AKCharacter::StaticClass();
    UClass* AttributeClass = UKAttributeSet_Global::StaticClass();
    if (!IsUsableObject(LocalPawn) || !IsUsableObject(CharacterClass) ||
        !IsUsableObject(AttributeClass) || !LocalPawn->IsA(CharacterClass))
    {
        return nullptr;
    }

    auto* Character = static_cast<AKCharacter*>(LocalPawn);
    UAbilitySystemComponent* AbilitySystem = Character->AbilitySystem;
    if (!IsUsableObject(AbilitySystem))
        return nullptr;

    auto& AttributeSets = AbilitySystem->SpawnedAttributes;
    if (AttributeSets.Num() <= 0 || AttributeSets.Num() > 256)
        return nullptr;

    for (int32 Index = 0; Index < AttributeSets.Num(); ++Index)
    {
        UAttributeSet* Candidate = AttributeSets[Index];
        if (IsUsableObject(Candidate) && Candidate->IsA(AttributeClass))
            return static_cast<UKAttributeSet_Global*>(Candidate);
    }

    return nullptr;
}

void SetDamageAttributeValue(
    UKAttributeSet_Global* AttributeSet,
    std::size_t Index,
    FGameplayAttributeData* Attribute,
    const DamageAttributeValues& Value,
    bool Notify = true)
{
    if (!AttributeSet || !Attribute)
        return;

    constexpr float Epsilon = 0.0001F;
    if (std::abs(Attribute->BaseValue - Value.Base) <= Epsilon &&
        std::abs(Attribute->CurrentValue - Value.Current) <= Epsilon)
    {
        return;
    }

    const FGameplayAttributeData OldValue = *Attribute;
    Attribute->BaseValue = Value.Base;
    Attribute->CurrentValue = Value.Current;
    if (Notify)
        NotifyDamageAttributeChanged(AttributeSet, Index, OldValue);
}

void RestoreOfflineDamageModifierLocked(bool Notify = true)
{
    DamageModifierState& State = g_DamageModifierState;
    UClass* AttributeClass = UKAttributeSet_Global::StaticClass();
    if (State.HasSnapshot && IsUsableObject(State.AttributeSet) &&
        IsUsableObject(AttributeClass) && State.AttributeSet->IsA(AttributeClass))
    {
        const auto Attributes = GetDamageAttributes(State.AttributeSet);
        for (std::size_t Index = 0; Index < Attributes.size(); ++Index)
        {
            SetDamageAttributeValue(
                State.AttributeSet,
                Index,
                Attributes[Index],
                State.OriginalValues[Index],
                Notify);
        }

        WriteDiagnosticLog(
            "Offline damage modifier restored: attribute_set=%p previous_multiplier=%.2f",
            State.AttributeSet,
            static_cast<double>(State.AppliedMultiplier));
    }

    State = DamageModifierState{};
    g_DamageModifierApplied.store(false, std::memory_order_release);
}

void RestoreOfflineDamageModifier(bool Notify = true)
{
    std::scoped_lock ModifierLock(g_DamageModifierMutex);
    RestoreOfflineDamageModifierLocked(Notify);
    g_DamageModifierStatus.store(DamageModifierStatus::Disabled, std::memory_order_relaxed);
}

void TickOfflineDamageModifier()
{
    bool Enabled{};
    bool DamageMultiplierEnabled{};
    bool IncludeStability{};
    bool OneHitMelee{};
    float Multiplier{1.0F};
    {
        std::scoped_lock SettingsLock(g_SettingsMutex);
        DamageMultiplierEnabled = g_Settings.EnableOfflineDamageModifier;
        OneHitMelee = g_Settings.OneHitMelee;
        Enabled = DamageMultiplierEnabled || OneHitMelee;
        IncludeStability = g_Settings.IncludeStabilityDamage;
        Multiplier = std::clamp(
            g_Settings.OfflineDamageMultiplier,
            kMinDamageMultiplier,
            kMaxDamageMultiplier);
        g_Settings.OfflineDamageMultiplier = Multiplier;
    }

    std::scoped_lock ModifierLock(g_DamageModifierMutex);
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire))
    {
        RestoreOfflineDamageModifierLocked();
        g_DamageModifierStatus.store(DamageModifierStatus::Disabled, std::memory_order_relaxed);
        return;
    }

    UWorld* World = UWorld::GetWorld();
    if (!IsOfflineWorld(World))
    {
        RestoreOfflineDamageModifierLocked();
        g_DamageModifierStatus.store(DamageModifierStatus::BlockedOnline, std::memory_order_relaxed);
        return;
    }

    APlayerController* Controller = GetLocalController(World);
    APawn* LocalPawn = Controller ? Controller->AcknowledgedPawn : nullptr;
    if (!IsUsableObject(LocalPawn))
    {
        RestoreOfflineDamageModifierLocked();
        g_DamageModifierStatus.store(DamageModifierStatus::WaitingForPlayer, std::memory_order_relaxed);
        return;
    }

    UKAttributeSet_Global* AttributeSet = FindGlobalDamageAttributeSet(LocalPawn);
    if (!IsUsableObject(AttributeSet))
    {
        RestoreOfflineDamageModifierLocked();
        g_DamageModifierStatus.store(DamageModifierStatus::WaitingForAttributes, std::memory_order_relaxed);
        return;
    }

    DamageModifierState& State = g_DamageModifierState;
    if (State.HasSnapshot && State.AttributeSet != AttributeSet)
        RestoreOfflineDamageModifierLocked();

    const auto Attributes = GetDamageAttributes(AttributeSet);
    if (!State.HasSnapshot)
    {
        for (const FGameplayAttributeData* Attribute : Attributes)
        {
            if (!IsSaneDamageAttribute(Attribute))
            {
                g_DamageModifierStatus.store(
                    DamageModifierStatus::WaitingForAttributes,
                    std::memory_order_relaxed);
                return;
            }
        }

        State.AttributeSet = AttributeSet;
        for (std::size_t Index = 0; Index < Attributes.size(); ++Index)
        {
            State.OriginalValues[Index] = {
                Attributes[Index]->BaseValue,
                Attributes[Index]->CurrentValue
            };
        }
        State.HasSnapshot = true;
    }

    for (std::size_t Index = 0; Index < Attributes.size(); ++Index)
    {
        float Scale = 1.0F;
        if ((Index == 1 || Index == 2) && OneHitMelee)
            Scale = kOneHitMeleeMultiplier;
        else if (DamageMultiplierEnabled && (Index != 0 || IncludeStability))
            Scale = Multiplier;
        State.AppliedValues[Index] = {
            State.OriginalValues[Index].Base * Scale,
            State.OriginalValues[Index].Current * Scale
        };
        SetDamageAttributeValue(
            AttributeSet,
            Index,
            Attributes[Index],
            State.AppliedValues[Index]);
    }

    const bool WasApplied =
        g_DamageModifierApplied.exchange(true, std::memory_order_acq_rel);
    const bool SettingsChanged =
        std::abs(State.AppliedMultiplier - Multiplier) > 0.0001F ||
        State.IncludedStability != IncludeStability ||
        State.AppliedOneHitMelee != OneHitMelee;
    State.AppliedMultiplier = Multiplier;
    State.IncludedStability = IncludeStability;
    State.AppliedOneHitMelee = OneHitMelee;
    g_DamageModifierStatus.store(DamageModifierStatus::Applied, std::memory_order_relaxed);

    if (!WasApplied || SettingsChanged)
    {
        WriteDiagnosticLog(
            "Offline damage modifier applied: attribute_set=%p multiplier=%s/%.2f one_hit_melee=%s stability=%s net_driver=null",
            AttributeSet,
            DamageMultiplierEnabled ? "on" : "off",
            static_cast<double>(Multiplier),
            OneHitMelee ? "yes" : "no",
            IncludeStability ? "yes" : "no");
    }
}

std::array<FGameplayAttributeData*, 3> GetMeleeRangeAttributes(
    UKAttributeSet_Item* AttributeSet)
{
    if (!AttributeSet)
        return {};

    return {
        &AttributeSet->Range,
        &AttributeSet->ShoveRange,
        &AttributeSet->HeavyShoveRange
    };
}

void NotifyMeleeRangeChanged(
    UKAttributeSet_Item* AttributeSet,
    std::size_t Index,
    const FGameplayAttributeData& OldValue)
{
    if (!AttributeSet)
        return;

    switch (Index)
    {
    case 0: AttributeSet->OnRep_Range(OldValue); break;
    case 1: AttributeSet->OnRep_ShoveRange(OldValue); break;
    case 2: AttributeSet->OnRep_HeavyShoveRange(OldValue); break;
    default: break;
    }
}

bool AttributeMatches(
    const FGameplayAttributeData* Attribute,
    const DamageAttributeValues& Values)
{
    constexpr float Epsilon = 0.001F;
    return Attribute &&
           std::abs(Attribute->BaseValue - Values.Base) <= Epsilon &&
           std::abs(Attribute->CurrentValue - Values.Current) <= Epsilon;
}

void RestoreMeleeRangeModifier(bool Notify = true)
{
    MeleeRangeState& State = g_MeleeRangeState;
    UClass* AttributeClass = UKAttributeSet_Item::StaticClass();
    if (State.HasSnapshot && IsUsableObject(State.AttributeSet) &&
        IsUsableObject(AttributeClass) && State.AttributeSet->IsA(AttributeClass))
    {
        const auto Attributes = GetMeleeRangeAttributes(State.AttributeSet);
        for (std::size_t Index = 0; Index < Attributes.size(); ++Index)
        {
            // Equipping another item can repopulate this shared attribute set.
            // Only restore values that still equal the values written by us.
            if (!AttributeMatches(Attributes[Index], State.AppliedValues[Index]))
                continue;

            const FGameplayAttributeData OldValue = *Attributes[Index];
            Attributes[Index]->BaseValue = State.OriginalValues[Index].Base;
            Attributes[Index]->CurrentValue = State.OriginalValues[Index].Current;
            if (Notify)
                NotifyMeleeRangeChanged(State.AttributeSet, Index, OldValue);
        }
        WriteDiagnosticLog("Double melee range restored: item=%p attribute_set=%p", State.Item, State.AttributeSet);
    }

    State = MeleeRangeState{};
    g_MeleeRangeApplied.store(false, std::memory_order_release);
}

AKItem* GetEquippedMainHandItem(AKCharacter* Character)
{
    if (!IsUsableObject(Character) || !IsUsableObject(Character->EquipmentComponent))
        return nullptr;

    const TScriptInterface<IKItemInterface> MainHand =
        Character->EquipmentComponent->GetMainHandItem();
    UObject* ItemObject = MainHand.GetObjectRef();
    UClass* ItemClass = AKItem::StaticClass();
    if (!IsUsableObject(ItemObject) || !IsUsableObject(ItemClass) ||
        !ItemObject->IsA(ItemClass))
    {
        return nullptr;
    }
    return static_cast<AKItem*>(ItemObject);
}

void TickMeleeRangeModifier(UWorld* World, APlayerController* Controller, bool Enabled)
{
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire) ||
        !IsOfflineWorld(World) ||
        !IsUsableObject(Controller) || !IsUsableObject(Controller->AcknowledgedPawn))
    {
        RestoreMeleeRangeModifier();
        return;
    }

    APawn* Pawn = Controller->AcknowledgedPawn;
    UClass* CharacterClass = AKCharacter::StaticClass();
    if (!IsUsableObject(CharacterClass) || !Pawn->IsA(CharacterClass))
    {
        RestoreMeleeRangeModifier();
        return;
    }

    auto* Character = static_cast<AKCharacter*>(Pawn);
    AKItem* Item = GetEquippedMainHandItem(Character);
    UKAttributeSet_Item* AttributeSet = Character->ItemAttributeSet;
    if (!IsUsableObject(Item) || !Item->GetIsMeleeWeapon() ||
        !IsUsableObject(AttributeSet))
    {
        RestoreMeleeRangeModifier();
        return;
    }

    MeleeRangeState& State = g_MeleeRangeState;
    if (State.HasSnapshot &&
        (State.Character != Character || State.Item != Item || State.AttributeSet != AttributeSet))
    {
        RestoreMeleeRangeModifier();
    }

    const auto Attributes = GetMeleeRangeAttributes(AttributeSet);
    if (!State.HasSnapshot)
    {
        for (const FGameplayAttributeData* Attribute : Attributes)
        {
            if (!Attribute || !std::isfinite(Attribute->BaseValue) ||
                !std::isfinite(Attribute->CurrentValue) ||
                Attribute->BaseValue <= 0.0F || Attribute->BaseValue > 100000.0F ||
                Attribute->CurrentValue <= 0.0F || Attribute->CurrentValue > 100000.0F)
            {
                return;
            }
        }

        State.Character = Character;
        State.Item = Item;
        State.AttributeSet = AttributeSet;
        for (std::size_t Index = 0; Index < Attributes.size(); ++Index)
        {
            State.OriginalValues[Index] = {
                Attributes[Index]->BaseValue,
                Attributes[Index]->CurrentValue
            };
        }
        State.HasSnapshot = true;
    }

    for (std::size_t Index = 0; Index < Attributes.size(); ++Index)
    {
        State.AppliedValues[Index] = {
            State.OriginalValues[Index].Base * 2.0F,
            State.OriginalValues[Index].Current * 2.0F
        };
        if (!AttributeMatches(Attributes[Index], State.AppliedValues[Index]))
        {
            const FGameplayAttributeData OldValue = *Attributes[Index];
            Attributes[Index]->BaseValue = State.AppliedValues[Index].Base;
            Attributes[Index]->CurrentValue = State.AppliedValues[Index].Current;
            NotifyMeleeRangeChanged(AttributeSet, Index, OldValue);
        }
    }

    if (!g_MeleeRangeApplied.exchange(true, std::memory_order_acq_rel))
        WriteDiagnosticLog("Double melee range applied: item=%p attribute_set=%p", Item, AttributeSet);
}

std::array<FGameplayAttributeData*, 21> GetWeaponModifierAttributes(
    UKAttributeSet_Item* AttributeSet)
{
    if (!AttributeSet)
        return {};
    return {
        &AttributeSet->ReloadSpeed,
        &AttributeSet->RateOfFire,
        &AttributeSet->PelletSpreadDegrees,
        &AttributeSet->SpreadDirectionDegrees,
        &AttributeSet->TargetingSpreadMultiplier,
        &AttributeSet->MovingSpreadMultiplier,
        &AttributeSet->CrouchingSpreadMultiplier,
        &AttributeSet->PerShotSpreadIncrease,
        &AttributeSet->MaxSpreadDegrees,
        &AttributeSet->InitialADSSpread,
        &AttributeSet->IronRecoilStrength,
        &AttributeSet->RecoilPitch,
        &AttributeSet->RecoilYawMin,
        &AttributeSet->RecoilYawMax,
        &AttributeSet->RecoilRollMin,
        &AttributeSet->RecoilRollMax,
        &AttributeSet->RecoilYawFirstShot,
        &AttributeSet->RecoilPitchFirstShot,
        &AttributeSet->RecoilRollFirstShot,
        &AttributeSet->AttackAndComboPlayRate,
        &AttributeSet->HeavyAttackPlayRate
    };
}

void NotifyWeaponAttributeChanged(
    UKAttributeSet_Item* AttributeSet,
    std::size_t Index,
    const FGameplayAttributeData& OldValue)
{
    if (!AttributeSet)
        return;
    switch (Index)
    {
    case 0: AttributeSet->OnRep_ReloadSpeed(OldValue); break;
    case 1: AttributeSet->OnRep_RateOfFire(OldValue); break;
    case 2: AttributeSet->OnRep_PelletSpreadDegrees(OldValue); break;
    case 3: AttributeSet->OnRep_SpreadDirectionDegrees(OldValue); break;
    case 4: AttributeSet->OnRep_TargetingSpreadMultiplier(OldValue); break;
    case 5: AttributeSet->OnRep_MovingSpreadMultiplier(OldValue); break;
    case 6: AttributeSet->OnRep_CrouchingSpreadMultiplier(OldValue); break;
    case 7: AttributeSet->OnRep_PerShotSpreadIncrease(OldValue); break;
    case 8: AttributeSet->OnRep_MaxSpreadDegrees(OldValue); break;
    case 9: AttributeSet->OnRep_InitialADSSpread(OldValue); break;
    case 10: AttributeSet->OnRep_IronRecoilStrength(OldValue); break;
    case 11: AttributeSet->OnRep_RecoilPitch(OldValue); break;
    case 12: AttributeSet->OnRep_RecoilYawMin(OldValue); break;
    case 13: AttributeSet->OnRep_RecoilYawMax(OldValue); break;
    case 14: AttributeSet->OnRep_RecoilRollMin(OldValue); break;
    case 15: AttributeSet->OnRep_RecoilRollMax(OldValue); break;
    case 16: AttributeSet->OnRep_RecoilYawFirstShot(OldValue); break;
    case 17: AttributeSet->OnRep_RecoilPitchFirstShot(OldValue); break;
    case 18: AttributeSet->OnRep_RecoilRollFirstShot(OldValue); break;
    case 19: AttributeSet->OnRep_AttackAndComboPlayRate(OldValue); break;
    case 20: AttributeSet->OnRep_HeavyAttackPlayRate(OldValue); break;
    default: break;
    }
}

void RestoreWeaponModifier(bool Notify = true)
{
    WeaponModifierState& State = g_WeaponModifierState;
    UClass* AttributeClass = UKAttributeSet_Item::StaticClass();
    if (State.HasSnapshot && IsUsableObject(State.AttributeSet) &&
        IsUsableObject(AttributeClass) && State.AttributeSet->IsA(AttributeClass))
    {
        const auto Attributes = GetWeaponModifierAttributes(State.AttributeSet);
        for (std::size_t Index = 0; Index < Attributes.size(); ++Index)
        {
            if (!AttributeMatches(Attributes[Index], State.AppliedValues[Index]))
                continue;
            const FGameplayAttributeData OldValue = *Attributes[Index];
            Attributes[Index]->BaseValue = State.OriginalValues[Index].Base;
            Attributes[Index]->CurrentValue = State.OriginalValues[Index].Current;
            if (Notify)
                NotifyWeaponAttributeChanged(State.AttributeSet, Index, OldValue);
        }
        WriteDiagnosticLog("Weapon modifiers restored: item=%p attribute_set=%p", State.Item, State.AttributeSet);
    }
    State = WeaponModifierState{};
    g_WeaponModifierApplied.store(false, std::memory_order_release);
}

void TickWeaponModifier(
    UWorld* World,
    APlayerController* Controller,
    bool NoRecoil,
    bool ModifySpread,
    float SpreadMultiplier,
    bool ModifyReload,
    float ReloadMultiplier,
    bool ModifyFireRate,
    float FireRateMultiplier)
{
    const bool Enabled = NoRecoil || ModifySpread || ModifyReload || ModifyFireRate;
    APawn* Pawn = IsUsableObject(Controller) ? Controller->AcknowledgedPawn : nullptr;
    UClass* CharacterClass = AKCharacter::StaticClass();
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire) ||
        !IsOfflineWorld(World) ||
        !IsUsableObject(Pawn) || !IsUsableObject(CharacterClass) || !Pawn->IsA(CharacterClass))
    {
        RestoreWeaponModifier();
        return;
    }

    auto* Character = static_cast<AKCharacter*>(Pawn);
    AKItem* Item = GetEquippedMainHandItem(Character);
    UKAttributeSet_Item* AttributeSet = Character->ItemAttributeSet;
    if (!IsUsableObject(Item) || !IsUsableObject(AttributeSet))
    {
        RestoreWeaponModifier();
        return;
    }

    WeaponModifierState& State = g_WeaponModifierState;
    if (State.HasSnapshot &&
        (State.Character != Character || State.Item != Item || State.AttributeSet != AttributeSet))
    {
        RestoreWeaponModifier();
    }

    const auto Attributes = GetWeaponModifierAttributes(AttributeSet);
    if (!State.HasSnapshot)
    {
        for (const FGameplayAttributeData* Attribute : Attributes)
        {
            if (!Attribute || !std::isfinite(Attribute->BaseValue) ||
                !std::isfinite(Attribute->CurrentValue) ||
                std::abs(Attribute->BaseValue) > 100000.0F ||
                std::abs(Attribute->CurrentValue) > 100000.0F)
            {
                return;
            }
        }
        State.Character = Character;
        State.Item = Item;
        State.AttributeSet = AttributeSet;
        for (std::size_t Index = 0; Index < Attributes.size(); ++Index)
            State.OriginalValues[Index] = {Attributes[Index]->BaseValue, Attributes[Index]->CurrentValue};
        State.HasSnapshot = true;
    }

    SpreadMultiplier = std::clamp(SpreadMultiplier, 0.0F, 2.0F);
    ReloadMultiplier = std::clamp(ReloadMultiplier, kMinWeaponSpeedMultiplier, kMaxWeaponSpeedMultiplier);
    FireRateMultiplier = std::clamp(FireRateMultiplier, kMinWeaponSpeedMultiplier, kMaxWeaponSpeedMultiplier);
    for (std::size_t Index = 0; Index < Attributes.size(); ++Index)
    {
        float Scale = 1.0F;
        if (Index == 0 && ModifyReload)
            Scale = ReloadMultiplier;
        else if ((Index == 1 || Index == 19 || Index == 20) && ModifyFireRate)
            Scale = FireRateMultiplier;
        else if (Index >= 2 && Index <= 9 && ModifySpread)
            Scale = SpreadMultiplier;
        else if (Index >= 10 && Index <= 18 && NoRecoil)
            Scale = 0.0F;

        State.AppliedValues[Index] = {
            State.OriginalValues[Index].Base * Scale,
            State.OriginalValues[Index].Current * Scale
        };
        if (!AttributeMatches(Attributes[Index], State.AppliedValues[Index]))
        {
            const FGameplayAttributeData OldValue = *Attributes[Index];
            Attributes[Index]->BaseValue = State.AppliedValues[Index].Base;
            Attributes[Index]->CurrentValue = State.AppliedValues[Index].Current;
            NotifyWeaponAttributeChanged(AttributeSet, Index, OldValue);
        }
    }

    if (!g_WeaponModifierApplied.exchange(true, std::memory_order_acq_rel))
        WriteDiagnosticLog("Weapon modifiers applied: item=%p attribute_set=%p", Item, AttributeSet);
}

UKAttributeSet_Stamina* FindStaminaAttributeSet(APawn* Pawn)
{
    UClass* CharacterClass = AKCharacter::StaticClass();
    UClass* StaminaClass = UKAttributeSet_Stamina::StaticClass();
    if (!IsUsableObject(Pawn) || !IsUsableObject(CharacterClass) ||
        !IsUsableObject(StaminaClass) || !Pawn->IsA(CharacterClass))
    {
        return nullptr;
    }
    UAbilitySystemComponent* AbilitySystem = static_cast<AKCharacter*>(Pawn)->AbilitySystem;
    if (!IsUsableObject(AbilitySystem) || AbilitySystem->SpawnedAttributes.Num() <= 0 ||
        AbilitySystem->SpawnedAttributes.Num() > 256)
    {
        return nullptr;
    }
    for (int32 Index = 0; Index < AbilitySystem->SpawnedAttributes.Num(); ++Index)
    {
        UAttributeSet* Candidate = AbilitySystem->SpawnedAttributes[Index];
        if (IsUsableObject(Candidate) && Candidate->IsA(StaminaClass))
            return static_cast<UKAttributeSet_Stamina*>(Candidate);
    }
    return nullptr;
}

void RestoreStaminaModifier(bool Notify = true)
{
    StaminaModifierState& State = g_StaminaModifierState;
    UClass* StaminaClass = UKAttributeSet_Stamina::StaticClass();
    if (State.HasSnapshot && IsUsableObject(State.AttributeSet) &&
        IsUsableObject(StaminaClass) && State.AttributeSet->IsA(StaminaClass))
    {
        const FGameplayAttributeData OldValue = State.AttributeSet->Stamina;
        State.AttributeSet->Stamina.BaseValue = State.OriginalValue.Base;
        State.AttributeSet->Stamina.CurrentValue = State.OriginalValue.Current;
        if (Notify)
            State.AttributeSet->OnRep_Stamina(OldValue);
        WriteDiagnosticLog("Infinite stamina restored: attribute_set=%p", State.AttributeSet);
    }
    State = StaminaModifierState{};
    g_StaminaModifierApplied.store(false, std::memory_order_release);
}

void TickStaminaModifier(UWorld* World, APlayerController* Controller, bool Enabled)
{
    APawn* Pawn = IsUsableObject(Controller) ? Controller->AcknowledgedPawn : nullptr;
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire) ||
        !IsOfflineWorld(World) || !IsUsableObject(Pawn))
    {
        RestoreStaminaModifier();
        return;
    }
    UKAttributeSet_Stamina* AttributeSet = FindStaminaAttributeSet(Pawn);
    if (!IsUsableObject(AttributeSet) || !std::isfinite(AttributeSet->MaxStamina.CurrentValue) ||
        AttributeSet->MaxStamina.CurrentValue <= 0.0F || AttributeSet->MaxStamina.CurrentValue > 100000.0F)
    {
        RestoreStaminaModifier();
        return;
    }
    StaminaModifierState& State = g_StaminaModifierState;
    if (State.HasSnapshot && State.AttributeSet != AttributeSet)
        RestoreStaminaModifier();
    if (!State.HasSnapshot)
    {
        State.AttributeSet = AttributeSet;
        State.OriginalValue = {AttributeSet->Stamina.BaseValue, AttributeSet->Stamina.CurrentValue};
        State.HasSnapshot = true;
    }
    const float Maximum = AttributeSet->MaxStamina.CurrentValue;
    State.AppliedValue = {Maximum, Maximum};
    if (!AttributeMatches(&AttributeSet->Stamina, State.AppliedValue))
    {
        const FGameplayAttributeData OldValue = AttributeSet->Stamina;
        AttributeSet->Stamina.BaseValue = Maximum;
        AttributeSet->Stamina.CurrentValue = Maximum;
        AttributeSet->OnRep_Stamina(OldValue);
    }
    if (!g_StaminaModifierApplied.exchange(true, std::memory_order_acq_rel))
        WriteDiagnosticLog("Infinite stamina applied: attribute_set=%p", AttributeSet);
}

void RestoreGodMode()
{
    GodModeState& State = g_GodModeState;
    if (State.Applied && IsUsableObject(State.Pawn))
        State.Pawn->bCanBeDamaged = State.OriginalCanBeDamaged;
    if (State.Applied)
        WriteDiagnosticLog("God mode restored: pawn=%p", State.Pawn);
    State = GodModeState{};
    g_GodModeApplied.store(false, std::memory_order_release);
}

void TickGodMode(UWorld* World, APlayerController* Controller, bool Enabled)
{
    APawn* Pawn = IsUsableObject(Controller) ? Controller->AcknowledgedPawn : nullptr;
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire) ||
        !IsOfflineWorld(World) || !IsUsableObject(Pawn))
    {
        RestoreGodMode();
        return;
    }
    GodModeState& State = g_GodModeState;
    if (State.Applied && State.Pawn != Pawn)
        RestoreGodMode();
    if (!State.Applied)
    {
        State.Pawn = Pawn;
        State.OriginalCanBeDamaged = Pawn->bCanBeDamaged;
        State.Applied = true;
        g_GodModeApplied.store(true, std::memory_order_release);
        WriteDiagnosticLog("God mode applied: pawn=%p", Pawn);
    }
    Pawn->bCanBeDamaged = false;
}

void RestoreSpeedModifier()
{
    SpeedModifierState& State = g_SpeedModifierState;
    if (State.HasSnapshot && IsUsableObject(State.Pawn) &&
        std::isfinite(State.Pawn->CustomTimeDilation) &&
        std::abs(State.Pawn->CustomTimeDilation - State.AppliedTimeDilation) <= 0.001F)
    {
        State.Pawn->CustomTimeDilation = State.OriginalTimeDilation;
        WriteDiagnosticLog("Player speed restored: pawn=%p", State.Pawn);
    }
    State = SpeedModifierState{};
    g_SpeedModifierApplied.store(false, std::memory_order_release);
}

void TickSpeedModifier(UWorld* World, APlayerController* Controller, bool Enabled, float Multiplier)
{
    Multiplier = std::clamp(Multiplier, kMinSpeedMultiplier, kMaxSpeedMultiplier);
    APawn* Pawn = IsUsableObject(Controller) ? Controller->AcknowledgedPawn : nullptr;
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire) ||
        !IsOfflineWorld(World) || !IsUsableObject(Pawn))
    {
        RestoreSpeedModifier();
        return;
    }

    SpeedModifierState& State = g_SpeedModifierState;
    if (State.HasSnapshot && State.Pawn != Pawn)
        RestoreSpeedModifier();

    if (!State.HasSnapshot)
    {
        const float Current = Pawn->CustomTimeDilation;
        if (!std::isfinite(Current) || Current < 0.01F || Current > 20.0F)
            return;
        State.Pawn = Pawn;
        State.OriginalTimeDilation = Current;
        State.HasSnapshot = true;
    }

    State.AppliedMultiplier = Multiplier;
    State.AppliedTimeDilation = State.OriginalTimeDilation * Multiplier;
    Pawn->CustomTimeDilation = State.AppliedTimeDilation;
    if (!g_SpeedModifierApplied.exchange(true, std::memory_order_acq_rel))
        WriteDiagnosticLog("Player speed applied: pawn=%p multiplier=%.2f", Pawn, static_cast<double>(Multiplier));
}

void ExecuteOfflineConsoleCommand(
    UWorld* World,
    APlayerController* Controller,
    const std::wstring& Command)
{
    if (!IsUsableObject(World) || Command.empty())
        return;
    UKismetSystemLibrary::ExecuteConsoleCommand(
        World,
        FString(Command.c_str()),
        IsUsableObject(Controller) ? Controller : nullptr);
}

void RestoreFullbright(UWorld* World, APlayerController* Controller)
{
    FullbrightState& State = g_FullbrightState;
    if (State.HasSnapshot && IsUsableObject(World))
    {
        std::wostringstream Eye;
        Eye << L"r.EyeAdaptationQuality " << State.EyeAdaptationQuality;
        ExecuteOfflineConsoleCommand(World, Controller, Eye.str());

        std::wostringstream Exposure;
        Exposure << L"r.ExposureOffset " << std::fixed << std::setprecision(3) << State.ExposureOffset;
        ExecuteOfflineConsoleCommand(World, Controller, Exposure.str());

        std::wostringstream Gamma;
        Gamma << L"r.TonemapperGamma " << std::fixed << std::setprecision(3) << State.TonemapperGamma;
        ExecuteOfflineConsoleCommand(World, Controller, Gamma.str());
        WriteDiagnosticLog("Fullbright restored");
    }
    State = FullbrightState{};
    g_FullbrightApplied.store(false, std::memory_order_release);
}

void TickFullbright(UWorld* World, APlayerController* Controller, bool Enabled, float Intensity)
{
    if (!IsUsableObject(World))
        return;
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire) || !IsOfflineWorld(World))
    {
        RestoreFullbright(World, Controller);
        return;
    }

    Intensity = std::clamp(Intensity, 1.0F, 6.0F);
    FullbrightState& State = g_FullbrightState;
    if (!State.HasSnapshot)
    {
        State.EyeAdaptationQuality = UKismetSystemLibrary::GetConsoleVariableIntValue(
            FString(L"r.EyeAdaptationQuality"));
        State.ExposureOffset = UKismetSystemLibrary::GetConsoleVariableFloatValue(
            FString(L"r.ExposureOffset"));
        State.TonemapperGamma = UKismetSystemLibrary::GetConsoleVariableFloatValue(
            FString(L"r.TonemapperGamma"));
        State.HasSnapshot = true;
    }

    if (!g_FullbrightApplied.load(std::memory_order_acquire) ||
        std::abs(State.AppliedIntensity - Intensity) > 0.001F)
    {
        ExecuteOfflineConsoleCommand(World, Controller, L"r.EyeAdaptationQuality 0");
        std::wostringstream Exposure;
        Exposure << L"r.ExposureOffset " << std::fixed << std::setprecision(2) << Intensity;
        ExecuteOfflineConsoleCommand(World, Controller, Exposure.str());
        std::wostringstream Gamma;
        Gamma << L"r.TonemapperGamma " << std::fixed << std::setprecision(2)
              << (1.0F + ((Intensity - 1.0F) * 0.35F));
        ExecuteOfflineConsoleCommand(World, Controller, Gamma.str());
        State.AppliedIntensity = Intensity;
        g_FullbrightApplied.store(true, std::memory_order_release);
        WriteDiagnosticLog("Fullbright applied: intensity=%.2f", static_cast<double>(Intensity));
    }
}

void RestoreGhostMode()
{
    GhostModeState& State = g_GhostModeState;
    if (State.Applied && IsUsableObject(State.Manager))
    {
        State.Manager->Cheat_UnIgnorePlayer();
        State.Manager->DebugToggleGhost();
        WriteDiagnosticLog("Ghost mode restored: manager=%p", State.Manager);
    }
    State = GhostModeState{};
    g_GhostModeApplied.store(false, std::memory_order_release);
}

void TickGhostMode(UWorld* World, APlayerController* Controller, bool Enabled)
{
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire) ||
        !IsOfflineWorld(World) || !IsUsableObject(Controller))
    {
        RestoreGhostMode();
        return;
    }

    GhostModeState& State = g_GhostModeState;
    if (State.Applied && State.Controller != Controller)
        RestoreGhostMode();
    if (State.Applied)
        return;

    if (!IsUsableObject(Controller->CheatManager))
        Controller->EnableCheats();

    UClass* CheatClass = UKCheatManager::StaticClass();
    if (!IsUsableObject(Controller->CheatManager) || !IsUsableObject(CheatClass) ||
        !Controller->CheatManager->IsA(CheatClass))
    {
        return;
    }

    auto* Manager = static_cast<UKCheatManager*>(Controller->CheatManager);
    Manager->DebugToggleGhost();
    Manager->Cheat_IgnorePlayer();
    State.Controller = Controller;
    State.Manager = Manager;
    State.Applied = true;
    g_GhostModeApplied.store(true, std::memory_order_release);
    WriteDiagnosticLog("Ghost mode applied: controller=%p manager=%p", Controller, Manager);
}

UKCheatManager* GetOfflineCheatManager(UWorld* World, APlayerController* Controller)
{
    if (!IsOfflineWorld(World) || !IsUsableObject(Controller))
        return nullptr;
    if (!IsUsableObject(Controller->CheatManager))
        Controller->EnableCheats();
    UClass* CheatClass = UKCheatManager::StaticClass();
    if (!IsUsableObject(Controller->CheatManager) || !IsUsableObject(CheatClass) ||
        !Controller->CheatManager->IsA(CheatClass))
    {
        return nullptr;
    }
    return static_cast<UKCheatManager*>(Controller->CheatManager);
}

void InvokeAmmoCheatMode(UKCheatManager* Manager, int Mode)
{
    if (!IsUsableObject(Manager))
        return;
    if (Mode == 1)
        Manager->Cheat_InfiniteAmmo();
    else if (Mode == 2)
        Manager->Cheat_BottomlessAmmo();
}

void RestoreAmmoCheat(UWorld* World)
{
    AmmoCheatState& State = g_AmmoCheatState;
    if (State.AppliedMode != 0 && State.World == World && IsOfflineWorld(World) &&
        IsUsableObject(State.Manager))
    {
        InvokeAmmoCheatMode(State.Manager, State.AppliedMode);
        WriteDiagnosticLog("Ammo training mode restored: mode=%d manager=%p", State.AppliedMode, State.Manager);
    }
    State = AmmoCheatState{};
    g_AmmoCheatApplied.store(false, std::memory_order_release);
}

void TickAmmoCheat(UWorld* World, APlayerController* Controller, int Mode)
{
    Mode = std::clamp(Mode, 0, 2);
    if (Mode == 0 || g_UnloadRequested.load(std::memory_order_acquire))
    {
        RestoreAmmoCheat(World);
        return;
    }
    if (!IsOfflineWorld(World) || !IsUsableObject(Controller))
    {
        if (g_AmmoCheatState.AppliedMode != 0)
            WriteDiagnosticLog("Ammo training mode tracking cleared: standalone world unavailable");
        g_AmmoCheatState = AmmoCheatState{};
        g_AmmoCheatApplied.store(false, std::memory_order_release);
        return;
    }
    UKCheatManager* Manager = GetOfflineCheatManager(World, Controller);
    if (!IsUsableObject(Manager))
        return;
    AmmoCheatState& State = g_AmmoCheatState;
    if (State.AppliedMode != 0 &&
        (State.World != World || State.Controller != Controller ||
         State.Manager != Manager || State.AppliedMode != Mode))
    {
        RestoreAmmoCheat(World);
    }
    if (State.AppliedMode == Mode)
        return;
    InvokeAmmoCheatMode(Manager, Mode);
    State.World = World;
    State.Controller = Controller;
    State.Manager = Manager;
    State.AppliedMode = Mode;
    g_AmmoCheatApplied.store(true, std::memory_order_release);
    WriteDiagnosticLog("Ammo training mode applied: mode=%d manager=%p net_driver=null", Mode, Manager);
}

void RestoreGlobalTime()
{
    GlobalTimeState& State = g_GlobalTimeState;
    if (State.HasSnapshot && IsUsableObject(State.World))
    {
        UGameplayStatics::SetGlobalTimeDilation(State.World, State.OriginalScale);
        WriteDiagnosticLog("Global time scale restored: %.3f", static_cast<double>(State.OriginalScale));
    }
    State = GlobalTimeState{};
    g_GlobalTimeApplied.store(false, std::memory_order_release);
}

void TickGlobalTime(UWorld* World, bool Enabled, float Scale)
{
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire) ||
        !IsOfflineWorld(World))
    {
        RestoreGlobalTime();
        return;
    }
    Scale = std::clamp(Scale, kMinGlobalTimeScale, kMaxGlobalTimeScale);
    GlobalTimeState& State = g_GlobalTimeState;
    if (State.HasSnapshot && State.World != World)
        RestoreGlobalTime();
    if (!State.HasSnapshot)
    {
        const float Current = UGameplayStatics::GetGlobalTimeDilation(World);
        if (!std::isfinite(Current) || Current < 0.001F || Current > 100.0F)
            return;
        State.World = World;
        State.OriginalScale = Current;
        State.HasSnapshot = true;
    }
    State.AppliedScale = Scale;
    const float Current = UGameplayStatics::GetGlobalTimeDilation(World);
    if (!std::isfinite(Current) || std::abs(Current - Scale) > 0.001F)
        UGameplayStatics::SetGlobalTimeDilation(World, Scale);
    if (!g_GlobalTimeApplied.exchange(true, std::memory_order_acq_rel))
        WriteDiagnosticLog("Global time scale applied: %.3f", static_cast<double>(Scale));
}

void RestorePhotoMode()
{
    PhotoModeState& State = g_PhotoModeState;
    if (State.Applied && IsOfflineWorld(State.World) &&
        IsUsableObject(State.Manager))
    {
        State.Manager->ToggleDebugCamera();
        WriteDiagnosticLog("Photo mode debug camera restored");
    }
    else if (State.Applied)
    {
        WriteDiagnosticLog("Photo mode state cleared without a game call because the standalone context changed");
    }
    State = PhotoModeState{};
    g_PhotoModeApplied.store(false, std::memory_order_release);
}

void TickPhotoMode(UWorld* World, APlayerController* Controller, bool Enabled)
{
    if (!Enabled || g_UnloadRequested.load(std::memory_order_acquire) ||
        !IsOfflineWorld(World) || !IsUsableObject(Controller))
    {
        RestorePhotoMode();
        return;
    }

    PhotoModeState& State = g_PhotoModeState;
    if (State.Applied && (State.World != World || State.Controller != Controller))
        RestorePhotoMode();
    if (State.Applied)
        return;

    UKCheatManager* Manager = GetOfflineCheatManager(World, Controller);
    if (!IsUsableObject(Manager))
        return;
    Manager->ToggleDebugCamera();
    State.World = World;
    State.Controller = Controller;
    State.Manager = Manager;
    State.Applied = true;
    g_PhotoModeApplied.store(true, std::memory_order_release);
    WriteDiagnosticLog("Photo mode debug camera enabled: time scale handled by global dilation");
}

void RestoreZombieTime()
{
    UClass* CharacterClass = AKCharacter::StaticClass();
    for (const ZombieTimeState& State : g_ZombieTimeStates)
    {
        if (IsUsableObject(State.Character) && IsUsableObject(CharacterClass) &&
            State.Character->IsA(CharacterClass) && std::isfinite(State.Character->CustomTimeDilation) &&
            std::abs(State.Character->CustomTimeDilation - State.AppliedScale) <= 0.001F)
        {
            State.Character->CustomTimeDilation = State.OriginalScale;
        }
    }
    if (!g_ZombieTimeStates.empty())
        WriteDiagnosticLog("Zombie time control restored: actors=%llu", static_cast<unsigned long long>(g_ZombieTimeStates.size()));
    g_ZombieTimeStates.clear();
    g_ZombieTimeApplied.store(false, std::memory_order_release);
}

void ApplyZombieTimeScale(AKCharacter* Character, float Scale)
{
    if (!IsUsableObject(Character))
        return;
    Scale = std::clamp(Scale, 0.0F, 2.0F);
    auto It = std::find_if(
        g_ZombieTimeStates.begin(),
        g_ZombieTimeStates.end(),
        [Character](const ZombieTimeState& State) { return State.Character == Character; });
    if (It == g_ZombieTimeStates.end())
    {
        if (g_ZombieTimeStates.size() >= 4096)
            return;
        const float Current = Character->CustomTimeDilation;
        if (!std::isfinite(Current) || Current < 0.0F || Current > 20.0F)
            return;
        g_ZombieTimeStates.push_back(ZombieTimeState{Character, Current, Current * Scale});
        It = std::prev(g_ZombieTimeStates.end());
    }
    It->AppliedScale = It->OriginalScale * Scale;
    Character->CustomTimeDilation = It->AppliedScale;
    g_ZombieTimeApplied.store(true, std::memory_order_release);
}

void ClearOfflineActionRequests()
{
    g_RequestDestroyAllAi.store(false, std::memory_order_relaxed);
    g_RequestSpawnItem.store(false, std::memory_order_relaxed);
    g_RequestGiveAmmo.store(false, std::memory_order_relaxed);
    g_RequestGiveSupplies.store(false, std::memory_order_relaxed);
    g_RequestTeleportObjective.store(false, std::memory_order_relaxed);
    g_RequestTeleportExtraction.store(false, std::memory_order_relaxed);
    g_RequestTeleportPickup.store(false, std::memory_order_relaxed);
    g_RequestPickupVacuum.store(false, std::memory_order_relaxed);
}

void ExecuteOfflineCheatActions(
    UWorld* World,
    APlayerController* Controller,
    bool AutoRegeneration,
    bool InfiniteFlashlight,
    const std::string& SpawnIdentifier,
    const std::string& AmmoIdentifier,
    int AmmoAmount,
    int SuppliesAmount)
{
    if (!IsOfflineWorld(World) || !IsUsableObject(Controller))
    {
        ClearOfflineActionRequests();
        return;
    }
    UKCheatManager* Manager = GetOfflineCheatManager(World, Controller);
    if (!IsUsableObject(Manager))
        return;

    if (g_RequestDestroyAllAi.exchange(false, std::memory_order_acq_rel))
    {
        Manager->DestroyAllAI();
        WriteDiagnosticLog("DestroyAllAI executed through game debug manager");
    }
    if (g_RequestSpawnItem.exchange(false, std::memory_order_acq_rel))
    {
        if (!SpawnIdentifier.empty())
        {
            Manager->DebugSpawnItem(FString(std::wstring(SpawnIdentifier.begin(), SpawnIdentifier.end()).c_str()));
            WriteDiagnosticLog("DebugSpawnItem executed: identifier=%s", SpawnIdentifier.c_str());
        }
        else
        {
            WriteDiagnosticLog("DebugSpawnItem ignored: identifier is empty");
        }
    }
    if (g_RequestGiveAmmo.exchange(false, std::memory_order_acq_rel))
    {
        if (!AmmoIdentifier.empty())
        {
            Manager->Cheat_GiveAmmo(
                FString(std::wstring(AmmoIdentifier.begin(), AmmoIdentifier.end()).c_str()),
                std::clamp(AmmoAmount, 1, 100000));
            WriteDiagnosticLog("Cheat_GiveAmmo executed: identifier=%s amount=%d", AmmoIdentifier.c_str(), AmmoAmount);
        }
        else
        {
            WriteDiagnosticLog("Cheat_GiveAmmo ignored: identifier is empty");
        }
    }
    if (g_RequestGiveSupplies.exchange(false, std::memory_order_acq_rel))
    {
        Manager->Cheat_GiveSupplies(std::clamp(SuppliesAmount, 1, 100000));
        WriteDiagnosticLog("Cheat_GiveSupplies executed: amount=%d", SuppliesAmount);
    }

    const UINT64 Now = GetTickCount64();
    APawn* Pawn = Controller->AcknowledgedPawn;
    UClass* CharacterClass = AKCharacter::StaticClass();
    if (AutoRegeneration && Now - g_LastAutoHealTick >= 1000 && IsUsableObject(Pawn) &&
        IsUsableObject(CharacterClass) && Pawn->IsA(CharacterClass))
    {
        auto* Character = static_cast<AKCharacter*>(Pawn);
        if (IsUsableObject(Character->HealthComponent))
        {
            const float Current = Character->GetCurrentHealth();
            const float Maximum = Character->HealthComponent->GetMaxHealth();
            if (std::isfinite(Current) && std::isfinite(Maximum) && Maximum > 0.0F && Current < Maximum)
                Manager->Cheat_SetHealth(Maximum);
        }
        g_LastAutoHealTick = Now;
    }
    if (InfiniteFlashlight && Now - g_LastFlashlightRechargeTick >= 1000)
    {
        Manager->Cheat_RechargeEquippedFlashlight(1.0F);
        g_LastFlashlightRechargeTick = Now;
    }
}

bool TeleportLocalPawnToActor(
    APlayerController* Controller,
    APawn* LocalPawn,
    AActor* Target,
    const char* Description)
{
    if (!IsUsableObject(Controller) || !IsUsableObject(LocalPawn) || !IsUsableObject(Target))
        return false;
    FVector Destination = Target->K2_GetActorLocation();
    Destination.Z += 100.0;
    const bool Teleported = LocalPawn->K2_TeleportTo(Destination, Controller->GetControlRotation());
    WriteDiagnosticLog(
        "Teleport action: target=%s actor=%p result=%s",
        Description ? Description : "unknown",
        Target,
        Teleported ? "success" : "failed");
    return Teleported;
}

void ExecuteWorldActionRequests(
    UWorld* World,
    APlayerController* Controller,
    APawn* LocalPawn,
    AActor* Objective,
    AActor* Extraction,
    AActor* Pickup,
    const std::vector<AActor*>& VacuumPickups)
{
    if (!IsOfflineWorld(World) ||
        !IsUsableObject(Controller) || !IsUsableObject(LocalPawn))
    {
        ClearOfflineActionRequests();
        return;
    }
    if (g_RequestTeleportObjective.exchange(false, std::memory_order_acq_rel))
        TeleportLocalPawnToActor(Controller, LocalPawn, Objective, "nearest-objective");
    if (g_RequestTeleportExtraction.exchange(false, std::memory_order_acq_rel))
        TeleportLocalPawnToActor(Controller, LocalPawn, Extraction, "nearest-extraction");
    if (g_RequestTeleportPickup.exchange(false, std::memory_order_acq_rel))
        TeleportLocalPawnToActor(Controller, LocalPawn, Pickup, "crosshair-pickup");

    if (g_RequestPickupVacuum.exchange(false, std::memory_order_acq_rel))
    {
        const FVector Center = LocalPawn->K2_GetActorLocation();
        std::size_t Moved{};
        constexpr double GoldenAngle = 2.399963229728653;
        for (std::size_t Index = 0; Index < VacuumPickups.size() && Index < 256; ++Index)
        {
            AActor* Actor = VacuumPickups[Index];
            if (!IsUsableObject(Actor))
                continue;
            const double Angle = static_cast<double>(Index) * GoldenAngle;
            const double Radius = 130.0 + (static_cast<double>(Index % 6) * 38.0);
            const FVector Destination{
                Center.X + (std::cos(Angle) * Radius),
                Center.Y + (std::sin(Angle) * Radius),
                Center.Z + 35.0
            };
            if (Actor->K2_TeleportTo(Destination, FRotator{}))
                ++Moved;
        }
        WriteDiagnosticLog(
            "Pickup vacuum executed: candidates=%llu moved=%llu",
            static_cast<unsigned long long>(VacuumPickups.size()),
            static_cast<unsigned long long>(Moved));
    }
}

void TickOfflineGameplayModifiers()
{
    TickOfflineDamageModifier();

    bool DoubleMeleeRange{};
    bool Fullbright{};
    bool GhostMode{};
    bool SpeedHack{};
    int AmmoCheatMode{};
    bool InfiniteStamina{};
    bool NoRecoil{};
    bool ModifyWeaponSpread{};
    bool ModifyReloadSpeed{};
    bool ModifyFireRate{};
    bool GodMode{};
    bool AutoHealthRegeneration{};
    bool InfiniteFlashlightBattery{};
    bool ModifyZombieTime{};
    bool ModifyGlobalTime{};
    bool PhotoMode{};
    float FullbrightIntensity{};
    float SpeedMultiplier{};
    float WeaponSpreadMultiplier{};
    float ReloadSpeedMultiplier{};
    float FireRateMultiplier{};
    float GlobalTimeScale{};
    float PhotoTimeScale{};
    int GiveAmmoAmount{};
    int GiveSuppliesAmount{};
    std::string SpawnIdentifier;
    std::string AmmoIdentifier;
    {
        std::scoped_lock SettingsLock(g_SettingsMutex);
        DoubleMeleeRange = g_Settings.DoubleMeleeRange;
        Fullbright = g_Settings.Fullbright;
        FullbrightIntensity = g_Settings.FullbrightIntensity;
        GhostMode = g_Settings.GhostMode;
        SpeedHack = g_Settings.SpeedHack;
        SpeedMultiplier = g_Settings.SpeedMultiplier;
        AmmoCheatMode = std::clamp(g_Settings.AmmoCheatMode, 0, 2);
        InfiniteStamina = g_Settings.InfiniteStamina;
        NoRecoil = g_Settings.NoRecoil;
        ModifyWeaponSpread = g_Settings.ModifyWeaponSpread;
        WeaponSpreadMultiplier = g_Settings.WeaponSpreadMultiplier;
        ModifyReloadSpeed = g_Settings.ModifyReloadSpeed;
        ReloadSpeedMultiplier = g_Settings.ReloadSpeedMultiplier;
        ModifyFireRate = g_Settings.ModifyFireRate;
        FireRateMultiplier = g_Settings.FireRateMultiplier;
        GodMode = g_Settings.GodMode;
        AutoHealthRegeneration = g_Settings.AutoHealthRegeneration;
        InfiniteFlashlightBattery = g_Settings.InfiniteFlashlightBattery;
        ModifyZombieTime = g_Settings.ModifyZombieTime;
        ModifyGlobalTime = g_Settings.ModifyGlobalTime;
        GlobalTimeScale = g_Settings.GlobalTimeScale;
        PhotoMode = g_Settings.PhotoMode;
        PhotoTimeScale = g_Settings.PhotoTimeScale;
        GiveAmmoAmount = g_Settings.GiveAmmoAmount;
        GiveSuppliesAmount = g_Settings.GiveSuppliesAmount;
        SpawnIdentifier = g_Settings.SpawnItemIdentifier;
        AmmoIdentifier = g_Settings.AmmoGrantIdentifier;
        g_Settings.FullbrightIntensity = std::clamp(FullbrightIntensity, 1.0F, 6.0F);
        g_Settings.SpeedMultiplier = std::clamp(
            SpeedMultiplier,
            kMinSpeedMultiplier,
            kMaxSpeedMultiplier);
        g_Settings.AmmoCheatMode = AmmoCheatMode;
        g_Settings.WeaponSpreadMultiplier = std::clamp(WeaponSpreadMultiplier, 0.0F, 2.0F);
        g_Settings.ReloadSpeedMultiplier = std::clamp(
            ReloadSpeedMultiplier, kMinWeaponSpeedMultiplier, kMaxWeaponSpeedMultiplier);
        g_Settings.FireRateMultiplier = std::clamp(
            FireRateMultiplier, kMinWeaponSpeedMultiplier, kMaxWeaponSpeedMultiplier);
        g_Settings.GlobalTimeScale = std::clamp(
            GlobalTimeScale, kMinGlobalTimeScale, kMaxGlobalTimeScale);
        g_Settings.PhotoTimeScale = std::clamp(
            PhotoTimeScale, kMinGlobalTimeScale, 1.0F);
        g_Settings.GiveAmmoAmount = std::clamp(GiveAmmoAmount, 1, 100000);
        g_Settings.GiveSuppliesAmount = std::clamp(GiveSuppliesAmount, 1, 100000);
    }

    UWorld* World = UWorld::GetWorld();
    APlayerController* Controller = GetLocalController(World);
    TickMeleeRangeModifier(World, Controller, DoubleMeleeRange);
    TickSpeedModifier(World, Controller, SpeedHack, SpeedMultiplier);
    TickFullbright(World, Controller, Fullbright, FullbrightIntensity);
    TickGhostMode(World, Controller, GhostMode);
    TickWeaponModifier(
        World,
        Controller,
        NoRecoil,
        ModifyWeaponSpread,
        WeaponSpreadMultiplier,
        ModifyReloadSpeed,
        ReloadSpeedMultiplier,
        ModifyFireRate,
        FireRateMultiplier);
    TickStaminaModifier(World, Controller, InfiniteStamina);
    TickGodMode(World, Controller, GodMode);
    TickAmmoCheat(World, Controller, AmmoCheatMode);
    TickPhotoMode(World, Controller, PhotoMode);
    TickGlobalTime(World, ModifyGlobalTime || PhotoMode, PhotoMode ? PhotoTimeScale : GlobalTimeScale);
    if (!ModifyZombieTime || g_UnloadRequested.load(std::memory_order_acquire) ||
        !IsOfflineWorld(World))
    {
        RestoreZombieTime();
    }
    ExecuteOfflineCheatActions(
        World,
        Controller,
        AutoHealthRegeneration,
        InfiniteFlashlightBattery,
        SpawnIdentifier,
        AmmoIdentifier,
        GiveAmmoAmount,
        GiveSuppliesAmount);
}

bool AnyGameplayModifierApplied()
{
    return g_DamageModifierApplied.load(std::memory_order_acquire) ||
           g_MeleeRangeApplied.load(std::memory_order_acquire) ||
           g_SpeedModifierApplied.load(std::memory_order_acquire) ||
           g_FullbrightApplied.load(std::memory_order_acquire) ||
           g_GhostModeApplied.load(std::memory_order_acquire) ||
           g_WeaponModifierApplied.load(std::memory_order_acquire) ||
           g_StaminaModifierApplied.load(std::memory_order_acquire) ||
           g_GodModeApplied.load(std::memory_order_acquire) ||
           g_GlobalTimeApplied.load(std::memory_order_acquire) ||
           g_ZombieTimeApplied.load(std::memory_order_acquire) ||
           g_AmmoCheatApplied.load(std::memory_order_acquire) ||
           g_PhotoModeApplied.load(std::memory_order_acquire);
}

std::wstring Utf8ToWide(const std::string& Text)
{
    if (Text.empty())
        return {};

    const int Required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        Text.data(),
        static_cast<int>(Text.size()),
        nullptr,
        0);

    if (Required <= 0)
        return std::wstring(Text.begin(), Text.end());

    std::wstring Result(static_cast<std::size_t>(Required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        Text.data(),
        static_cast<int>(Text.size()),
        Result.data(),
        Required);
    return Result;
}

void DrawText(
    AHUD* Hud,
    const std::wstring& Text,
    float X,
    float Y,
    const FLinearColor& Color,
    float Scale = 1.0F)
{
    if (!Hud || Text.empty())
        return;

    Hud->DrawText(FString(Text.c_str()), Color, X, Y, nullptr, Scale, false);
}

void DrawCenteredText(
    AHUD* Hud,
    const std::wstring& Text,
    float CenterX,
    float Y,
    const FLinearColor& Color,
    float Scale = 1.0F)
{
    if (!Hud || Text.empty())
        return;

    float Width = 0.0F;
    float Height = 0.0F;
    Hud->GetTextSize(FString(Text.c_str()), &Width, &Height, nullptr, Scale);
    const float X = std::isfinite(Width) && Width > 0.0F ? CenterX - (Width * 0.5F) : CenterX;
    DrawText(Hud, Text, X, Y, Color, Scale);
}

void DrawBox(
    AHUD* Hud,
    float Left,
    float Top,
    float Width,
    float Height,
    const FLinearColor& Color,
    float Thickness = 1.5F)
{
    if (!Hud || Width <= 0.0F || Height <= 0.0F)
        return;

    Hud->DrawLine(Left, Top, Left + Width, Top, Color, Thickness);
    Hud->DrawLine(Left + Width, Top, Left + Width, Top + Height, Color, Thickness);
    Hud->DrawLine(Left + Width, Top + Height, Left, Top + Height, Color, Thickness);
    Hud->DrawLine(Left, Top + Height, Left, Top, Color, Thickness);
}

void DrawPanel(AHUD* Hud, float Left, float Top, float Width, float Height)
{
    if (!Hud)
        return;

    Hud->DrawRect(MakeColor(0.0F, 0.0F, 0.0F, 0.58F), Left + 8.0F, Top + 8.0F, Width, Height);
    const FLinearColor Background = MakeColor(
        g_Settings.MenuBackground[0], g_Settings.MenuBackground[1],
        g_Settings.MenuBackground[2], g_Settings.MenuBackground[3]);
    const FLinearColor Accent = MakeColor(
        g_Settings.MenuAccent[0], g_Settings.MenuAccent[1],
        g_Settings.MenuAccent[2], g_Settings.MenuAccent[3]);
    Hud->DrawRect(Background, Left, Top, Width, Height);
    Hud->DrawRect(Accent, Left, Top, 4.0F, Height);
    Hud->DrawRect(Accent, Left, Top, Width, 2.0F);
    DrawBox(Hud, Left, Top, Width, Height, Accent, 1.5F);
}

void DrawHealthBar(AHUD* Hud, AKCharacter* Character, float Left, float Top, float Height)
{
    if (!IsUsableObject(Hud) || !IsUsableObject(Character) ||
        !IsUsableObject(Character->HealthComponent) || Height <= 0.0F)
        return;

    const float Health = Character->GetCurrentHealth();
    const float MaxHealth = Character->HealthComponent->GetMaxHealth();
    if (!std::isfinite(Health) || !std::isfinite(MaxHealth) || MaxHealth <= 0.0F)
        return;

    const float Ratio = std::clamp(Health / MaxHealth, 0.0F, 1.0F);
    constexpr float Width = 4.0F;
    const float FilledHeight = Height * Ratio;
    const FLinearColor FillColor = MakeColor(1.0F - Ratio, Ratio, 0.12F);
    Hud->DrawRect(MakeColor(0.04F, 0.04F, 0.04F, 0.90F), Left - 8.0F, Top, Width, Height);
    Hud->DrawRect(FillColor, Left - 8.0F, Top + Height - FilledHeight, Width, FilledHeight);
}

void DrawPickupMarker(
    AHUD* Hud,
    APlayerController* Controller,
    const FVector& WorldLocation,
    const std::wstring& Label,
    const FLinearColor& Color)
{
    if (!Hud || !Controller)
        return;

    FVector2D Screen{};
    if (!Controller->ProjectWorldLocationToScreen(WorldLocation, &Screen, false))
        return;

    if (!std::isfinite(Screen.X) || !std::isfinite(Screen.Y))
        return;

    const float MarkerRadius = g_Settings.MarkerSize;
    const float X = static_cast<float>(Screen.X);
    const float Y = static_cast<float>(Screen.Y);
    Hud->DrawLine(X - MarkerRadius, Y, X + MarkerRadius, Y, Color, 2.0F);
    Hud->DrawLine(X, Y - MarkerRadius, X, Y + MarkerRadius, Color, 2.0F);
    DrawBox(Hud, X - MarkerRadius, Y - MarkerRadius, MarkerRadius * 2.0F, MarkerRadius * 2.0F, Color);
    DrawText(Hud, Label, X + MarkerRadius + 4.0F, Y - 9.0F, Color, g_Settings.TextScale);
}

double DistanceMeters(const FVector& A, const FVector& B)
{
    const double DX = A.X - B.X;
    const double DY = A.Y - B.Y;
    const double DZ = A.Z - B.Z;
    return std::sqrt((DX * DX) + (DY * DY) + (DZ * DZ)) / 100.0;
}

bool GetCanvasCenter(AHUD* Hud, FVector2D* Center)
{
    if (!Hud || !Hud->Canvas || !Center)
        return false;

    const float Width = Hud->Canvas->ClipX > 100.0F
        ? Hud->Canvas->ClipX
        : static_cast<float>(Hud->Canvas->SizeX);
    const float Height = Hud->Canvas->ClipY > 100.0F
        ? Hud->Canvas->ClipY
        : static_cast<float>(Hud->Canvas->SizeY);
    if (!std::isfinite(Width) || !std::isfinite(Height) || Width <= 100.0F || Height <= 100.0F)
        return false;

    const float OriginX = std::isfinite(Hud->Canvas->OrgX) ? Hud->Canvas->OrgX : 0.0F;
    const float OriginY = std::isfinite(Hud->Canvas->OrgY) ? Hud->Canvas->OrgY : 0.0F;
    Center->X = static_cast<double>(OriginX + (Width * 0.5F));
    Center->Y = static_cast<double>(OriginY + (Height * 0.5F));
    return true;
}

void DrawAimFovCircle(AHUD* Hud, const FVector2D& Center, float Radius, bool TargetLocked)
{
    if (!Hud || !std::isfinite(Radius) || Radius <= 1.0F)
        return;

    constexpr int Segments = 72;
    constexpr double TwoPi = 6.28318530717958647692;
    const FLinearColor Color = TargetLocked
        ? MakeColor(0.28F, 1.0F, 0.48F, 0.92F)
        : MakeColor(0.22F, 0.78F, 1.0F, 0.75F);
    for (int Index = 0; Index < Segments; ++Index)
    {
        const double AngleA = TwoPi * static_cast<double>(Index) / static_cast<double>(Segments);
        const double AngleB = TwoPi * static_cast<double>(Index + 1) / static_cast<double>(Segments);
        Hud->DrawLine(
            static_cast<float>(Center.X + (std::cos(AngleA) * Radius)),
            static_cast<float>(Center.Y + (std::sin(AngleA) * Radius)),
            static_cast<float>(Center.X + (std::cos(AngleB) * Radius)),
            static_cast<float>(Center.Y + (std::sin(AngleB) * Radius)),
            Color,
            1.25F);
    }
}

FLinearColor ArrayColor(const float* Color)
{
    return Color ? MakeColor(Color[0], Color[1], Color[2], Color[3])
                 : MakeColor(1.0F, 1.0F, 1.0F, 1.0F);
}

SkeletonBoneSet ResolveSkeletonBones(USkeletalMeshComponent* Mesh)
{
    SkeletonBoneSet Result{};
    if (!IsUsableObject(Mesh))
        return Result;
    static constexpr std::array<std::string_view, 16> Wanted{
        "head", "neck_01", "spine_03", "spine_02", "spine_01", "pelvis",
        "upperarm_l", "lowerarm_l", "hand_l", "upperarm_r", "lowerarm_r", "hand_r",
        "thigh_l", "calf_l", "thigh_r", "calf_r"};
    const int32 BoneCount = Mesh->GetNumBones();
    if (BoneCount <= 0 || BoneCount > 1024)
        return Result;
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        const FName BoneName = Mesh->GetBoneName(BoneIndex);
        std::string Name = BoneName.ToString();
        std::transform(Name.begin(), Name.end(), Name.begin(),
            [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
        for (std::size_t WantedIndex = 0; WantedIndex < Wanted.size(); ++WantedIndex)
        {
            if (!Result.Valid[WantedIndex] && Name == Wanted[WantedIndex])
            {
                Result.Names[WantedIndex] = BoneName;
                Result.Valid[WantedIndex] = true;
            }
        }
    }
    return Result;
}

const SkeletonBoneSet& GetSkeletonBones(USkeletalMeshComponent* Mesh)
{
    auto Existing = g_SkeletonCache.find(Mesh);
    if (Existing != g_SkeletonCache.end())
        return Existing->second;
    if (g_SkeletonCache.size() > 4096)
        g_SkeletonCache.clear();
    return g_SkeletonCache.emplace(Mesh, ResolveSkeletonBones(Mesh)).first->second;
}

bool ProjectBone(
    USkeletalMeshComponent* Mesh,
    const SkeletonBoneSet& Bones,
    std::size_t Index,
    APlayerController* Controller,
    FVector2D* Screen,
    FVector* World = nullptr)
{
    if (!IsUsableObject(Mesh) || !IsUsableObject(Controller) || !Screen ||
        Index >= Bones.Names.size() || !Bones.Valid[Index])
    {
        return false;
    }
    const FVector Location = Mesh->GetSocketLocation(Bones.Names[Index]);
    if (World)
        *World = Location;
    return Controller->ProjectWorldLocationToScreen(Location, Screen, false) &&
           std::isfinite(Screen->X) && std::isfinite(Screen->Y);
}

void DrawCharacterSkeleton(AHUD* Hud, APlayerController* Controller, AKCharacter* Character)
{
    if (!IsUsableObject(Hud) || !IsUsableObject(Controller) || !IsUsableObject(Character) ||
        !IsUsableObject(Character->Mesh))
    {
        return;
    }
    const SkeletonBoneSet& Bones = GetSkeletonBones(Character->Mesh);
    std::array<FVector2D, 16> Points{};
    std::array<bool, 16> Visible{};
    for (std::size_t Index = 0; Index < Points.size(); ++Index)
        Visible[Index] = ProjectBone(Character->Mesh, Bones, Index, Controller, &Points[Index]);
    static constexpr std::array<std::array<std::size_t, 2>, 15> Links{{
        {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5},
        {2, 6}, {6, 7}, {7, 8}, {2, 9}, {9, 10}, {10, 11},
        {5, 12}, {12, 13}, {5, 14}, {14, 15}}};
    const FLinearColor Color = ArrayColor(g_Settings.SkeletonColor);
    for (const auto& Link : Links)
    {
        if (!Visible[Link[0]] || !Visible[Link[1]])
            continue;
        Hud->DrawLine(
            static_cast<float>(Points[Link[0]].X), static_cast<float>(Points[Link[0]].Y),
            static_cast<float>(Points[Link[1]].X), static_cast<float>(Points[Link[1]].Y),
            Color, std::max(1.0F, g_Settings.BoxThickness));
    }
}

std::wstring ZombieTypeLabel(AKCharacter* Character)
{
    if (!IsUsableObject(Character))
        return L"Zombie";
    auto* Zombie = static_cast<ABP_Zombie_C*>(Character);
    if (!IsUsableObject(Zombie->KZombie))
        return L"Zombie";
    const wchar_t* Type = Zombie->KZombie->GetType() == EKZombieType::Child ? L"Child" : L"Adult";
    const wchar_t* SubType = L"Zombie";
    switch (Zombie->KZombie->GetSubType())
    {
    case EKZombieSubType::Shambler: SubType = L"Shambler"; break;
    case EKZombieSubType::Walker: SubType = L"Walker"; break;
    case EKZombieSubType::Runner: SubType = L"Runner"; break;
    default: break;
    }
    std::wstring Label = Type;
    Label += L" ";
    Label += SubType;
    if (Zombie->KZombie->GetState() == EKZombieState::Crawler)
        Label += L" (Crawler)";
    return Label;
}

void DrawRadar(
    AHUD* Hud,
    const FVector2D& ScreenCenter,
    const FVector& LocalLocation,
    double ViewYawDegrees,
    const std::vector<RadarPoint>& Points)
{
    if (!Hud || !g_Settings.ShowRadar)
        return;
    constexpr double Pi = 3.14159265358979323846;
    const float Size = std::clamp(g_Settings.RadarSize, 120.0F, 320.0F);
    const float Left = static_cast<float>((ScreenCenter.X * 2.0) - Size - 24.0);
    const float Top = 92.0F;
    const float Half = Size * 0.5F;
    const float CenterX = Left + Half;
    const float CenterY = Top + Half;
    Hud->DrawRect(MakeColor(0.01F, 0.02F, 0.035F, 0.72F), Left, Top, Size, Size);
    DrawBox(Hud, Left, Top, Size, Size, ArrayColor(g_Settings.MenuAccent), 1.2F);
    Hud->DrawLine(CenterX, Top, CenterX, Top + Size, MakeColor(0.25F, 0.36F, 0.48F, 0.55F), 1.0F);
    Hud->DrawLine(Left, CenterY, Left + Size, CenterY, MakeColor(0.25F, 0.36F, 0.48F, 0.55F), 1.0F);
    const double Yaw = ViewYawDegrees * Pi / 180.0;
    const double Range = std::max(25.0, static_cast<double>(g_Settings.RadarRangeMeters));
    for (const RadarPoint& Point : Points)
    {
        if (Point.Distance > Range)
            continue;
        const double DX = (Point.Location.X - LocalLocation.X) / 100.0;
        const double DY = (Point.Location.Y - LocalLocation.Y) / 100.0;
        const double Right = (-std::sin(Yaw) * DX) + (std::cos(Yaw) * DY);
        const double Forward = (std::cos(Yaw) * DX) + (std::sin(Yaw) * DY);
        const float X = CenterX + static_cast<float>(std::clamp(Right / Range, -1.0, 1.0) * (Half - 8.0F));
        const float Y = CenterY - static_cast<float>(std::clamp(Forward / Range, -1.0, 1.0) * (Half - 8.0F));
        const FLinearColor Color = Point.IsZombie ? ArrayColor(g_Settings.ZombieEspColor)
                                                  : CategoryColor(Point.Category);
        const float Dot = Point.IsZombie ? 4.0F : 3.0F;
        Hud->DrawRect(Color, X - Dot, Y - Dot, Dot * 2.0F, Dot * 2.0F);
    }
    Hud->DrawRect(ArrayColor(g_Settings.PlayerEspColor), CenterX - 4.0F, CenterY - 4.0F, 8.0F, 8.0F);
    DrawText(Hud, L"RADAR  " + std::to_wstring(static_cast<int>(Range)) + L"m", Left + 7.0F, Top + 5.0F,
        MakeColor(0.80F, 0.90F, 1.0F), 0.72F);
}

void DrawThreatIndicators(
    AHUD* Hud,
    const FVector2D& ScreenCenter,
    const FVector& LocalLocation,
    double ViewYawDegrees,
    const std::vector<ThreatCandidate>& Threats)
{
    if (!Hud || !g_Settings.ShowThreatIndicators || Threats.empty())
        return;
    constexpr double Pi = 3.14159265358979323846;
    const double Yaw = ViewYawDegrees * Pi / 180.0;
    const float Radius = static_cast<float>(std::max(40.0, std::min(ScreenCenter.X, ScreenCenter.Y) - 70.0));
    const FLinearColor Color = ArrayColor(g_Settings.ThreatColor);
    const std::size_t Limit = std::min<std::size_t>(Threats.size(), 12);
    for (std::size_t Index = 0; Index < Limit; ++Index)
    {
        const double DX = Threats[Index].Location.X - LocalLocation.X;
        const double DY = Threats[Index].Location.Y - LocalLocation.Y;
        const double Relative = std::atan2(DY, DX) - Yaw;
        const float DirectionX = static_cast<float>(std::sin(Relative));
        const float DirectionY = static_cast<float>(-std::cos(Relative));
        const float X = static_cast<float>(ScreenCenter.X) + (DirectionX * Radius);
        const float Y = static_cast<float>(ScreenCenter.Y) + (DirectionY * Radius);
        const float SideX = -DirectionY * 8.0F;
        const float SideY = DirectionX * 8.0F;
        Hud->DrawLine(X, Y, X - (DirectionX * 18.0F) + SideX, Y - (DirectionY * 18.0F) + SideY, Color, 2.0F);
        Hud->DrawLine(X, Y, X - (DirectionX * 18.0F) - SideX, Y - (DirectionY * 18.0F) - SideY, Color, 2.0F);
        Hud->DrawLine(X - (DirectionX * 18.0F) + SideX, Y - (DirectionY * 18.0F) + SideY,
                      X - (DirectionX * 18.0F) - SideX, Y - (DirectionY * 18.0F) - SideY, Color, 2.0F);
    }
    std::wostringstream Label;
    Label << L"HOSTILES " << Threats.size() << L"  nearest " << std::fixed << std::setprecision(0)
          << Threats.front().Distance << L"m";
    DrawCenteredText(Hud, Label.str(), static_cast<float>(ScreenCenter.X), 88.0F, Color, 0.82F);
}

const wchar_t* FireModeLabel(EKFireMode Mode)
{
    switch (Mode)
    {
    case EKFireMode::Single: return L"SINGLE";
    case EKFireMode::Burst: return L"BURST";
    case EKFireMode::Auto: return L"AUTO";
    case EKFireMode::ToolMode: return L"TOOL";
    default: return L"N/A";
    }
}

void DrawWeaponInformation(AHUD* Hud, const FVector2D& ScreenCenter, APawn* LocalPawn)
{
    if (!Hud || !g_Settings.ShowWeaponInfo || !IsUsableObject(LocalPawn) ||
        !LocalPawn->IsA(AKCharacter::StaticClass()))
    {
        return;
    }
    auto* Character = static_cast<AKCharacter*>(LocalPawn);
    AKItem* Item = GetEquippedMainHandItem(Character);
    if (!IsUsableObject(Item) || !IsUsableObject(Item->Class))
        return;
    const bool Firearm = Item->GetIsFirearm();
    const int Loaded = Firearm ? Item->GetCurrentAmmo(true) : 0;
    const int Capacity = Firearm ? Item->GetAmmoCapacity(true) : 0;
    int Reserve = 0;
    if (Firearm && IsUsableObject(Character->InventoryComponent))
        Reserve = Character->InventoryComponent->GetAmmoCount(Item->GetAmmoType());
    float Durability = -1.0F;
    float MaxDurability = -1.0F;
    UClass* DisposableClass = UKDisposableWeaponComponent::StaticClass();
    if (IsUsableObject(DisposableClass))
    {
        UActorComponent* Component = Item->GetComponentByClass(DisposableClass);
        if (IsUsableObject(Component) && Component->IsA(DisposableClass))
        {
            auto* Disposable = static_cast<UKDisposableWeaponComponent*>(Component);
            Durability = Disposable->GetDurability();
            MaxDurability = Disposable->GetMaxDurability();
        }
    }
    std::wstring Name = Utf8ToWide(Item->Class->GetName());
    if (Name.empty()) Name = L"Equipped item";
    std::wostringstream Line;
    Line << Name;
    if (Firearm)
        Line << L"  |  " << Loaded << L"/" << Capacity << L"  reserve " << Reserve
             << L"  |  " << FireModeLabel(Item->GetFireMode());
    if (std::isfinite(Durability) && std::isfinite(MaxDurability) && MaxDurability > 0.0F)
        Line << L"  |  durability " << std::fixed << std::setprecision(0)
             << (100.0F * std::clamp(Durability / MaxDurability, 0.0F, 1.0F)) << L"%";
    const float Width = 520.0F;
    const float Left = static_cast<float>((ScreenCenter.X * 2.0) - Width - 24.0);
    const float Top = static_cast<float>((ScreenCenter.Y * 2.0) - 58.0);
    Hud->DrawRect(MakeColor(0.01F, 0.02F, 0.04F, 0.72F), Left, Top, Width, 34.0F);
    DrawText(Hud, Line.str(), Left + 10.0F, Top + 8.0F, MakeColor(0.88F, 0.95F, 1.0F), 0.78F);
}

void DrawObjectiveTracker(AHUD* Hud, std::vector<ObjectiveCandidate> Objectives)
{
    if (!Hud || !g_Settings.ShowObjectiveTracker || Objectives.empty())
        return;
    std::stable_sort(Objectives.begin(), Objectives.end(),
        [](const ObjectiveCandidate& Left, const ObjectiveCandidate& Right)
        {
            if (Left.Complete != Right.Complete) return !Left.Complete;
            if (Left.Active != Right.Active) return Left.Active;
            return Left.Distance < Right.Distance;
        });
    const std::size_t Limit = std::min<std::size_t>(Objectives.size(), 6);
    const float Height = 35.0F + (static_cast<float>(Limit) * 20.0F);
    Hud->DrawRect(MakeColor(0.01F, 0.02F, 0.04F, 0.68F), 20.0F, 90.0F, 360.0F, Height);
    DrawText(Hud, L"OBJECTIVES", 30.0F, 98.0F, CategoryColor(MarkerCategory::Objective), 0.82F);
    for (std::size_t Index = 0; Index < Limit; ++Index)
    {
        const ObjectiveCandidate& Objective = Objectives[Index];
        std::wostringstream Line;
        Line << (Objective.Complete ? L"[DONE] " : (Objective.Active ? L"[ACTIVE] " : L"[WAIT] "))
             << Objective.Name << L"  " << std::fixed << std::setprecision(0) << Objective.Distance << L"m";
        DrawText(Hud, Line.str(), 30.0F, 121.0F + (static_cast<float>(Index) * 20.0F),
            Objective.Complete ? MakeColor(0.35F, 0.78F, 0.48F) : CategoryColor(MarkerCategory::Objective), 0.72F);
    }
}

void DrawDiagnosticOverlay(AHUD* Hud, const FVector2D& ScreenCenter)
{
    if (!Hud || !g_Settings.ShowDiagnosticOverlay)
        return;
    std::vector<std::string> Lines;
    {
        std::scoped_lock Lock(g_DiagnosticConsoleMutex);
        const std::size_t Count = std::min<std::size_t>(g_DiagnosticConsole.size(),
            static_cast<std::size_t>(std::clamp(g_Settings.DiagnosticOverlayLines, 3, 15)));
        Lines.assign(g_DiagnosticConsole.end() - static_cast<std::ptrdiff_t>(Count), g_DiagnosticConsole.end());
    }
    float Y = static_cast<float>((ScreenCenter.Y * 2.0) - 85.0 - (Lines.size() * 16.0));
    for (std::string& Line : Lines)
    {
        while (!Line.empty() && (Line.back() == '\n' || Line.back() == '\r')) Line.pop_back();
        DrawText(Hud, Utf8ToWide(Line), 20.0F, Y, MakeColor(0.62F, 0.82F, 0.92F, 0.88F), 0.62F);
        Y += 16.0F;
    }
}

FRotator CalculateLookRotation(const FVector& From, const FVector& To)
{
    constexpr double RadiansToDegrees = 57.2957795130823208768;
    const double DX = To.X - From.X;
    const double DY = To.Y - From.Y;
    const double DZ = To.Z - From.Z;
    const double Horizontal = std::sqrt((DX * DX) + (DY * DY));
    return FRotator{
        std::atan2(DZ, std::max(0.0001, Horizontal)) * RadiansToDegrees,
        std::atan2(DY, DX) * RadiansToDegrees,
        0.0
    };
}

double InterpolateAngle(double Current, double Target, double Alpha)
{
    const double Delta = FRotator::NormalizeAxis(Target - Current);
    return FRotator::NormalizeAxis(Current + (Delta * std::clamp(Alpha, 0.0, 1.0)));
}

void ApplyZombieAim(APlayerController* Controller, const AimTargetCandidate& Target)
{
    if (!g_Settings.EnableZombieAimbot || !IsUsableObject(Controller) ||
        !IsUsableObject(Target.Character) ||
        g_MenuOpen.load(std::memory_order_relaxed))
    {
        return;
    }

    const bool PrimaryHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool SecondaryHeld = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const bool AimHeld = SecondaryHeld || (g_Settings.AimHipfireMode && PrimaryHeld);
    if (!AimHeld)
        return;

    FVector ViewLocation{};
    FRotator ViewRotation{};
    Controller->GetPlayerViewPoint(&ViewLocation, &ViewRotation);
    if (!std::isfinite(ViewLocation.X) || !std::isfinite(ViewLocation.Y) ||
        !std::isfinite(ViewLocation.Z))
    {
        return;
    }

    const FRotator Desired = CalculateLookRotation(ViewLocation, Target.AimLocation);
    const FRotator Current = Controller->GetControlRotation();
    static UINT64 LastAimTick{};
    const UINT64 CurrentTick = GetTickCount64();
    const double DeltaSeconds = LastAimTick == 0
        ? (1.0 / 60.0)
        : std::clamp(static_cast<double>(CurrentTick - LastAimTick) / 1000.0, 0.001, 0.050);
    LastAimTick = CurrentTick;

    const double Speed = std::clamp(
        static_cast<double>(g_Settings.AimSpeed),
        static_cast<double>(kMinAimSpeed),
        static_cast<double>(kMaxAimSpeed));
    const double BaseAlpha = 1.0 - std::exp(-Speed * DeltaSeconds);
    const double RcsBoost = g_Settings.EnableSimpleRcs && PrimaryHeld
        ? (1.0 + (std::clamp(static_cast<double>(g_Settings.RcsStrength), 0.0, 1.0) * 3.0))
        : 1.0;

    FRotator Smoothed{};
    Smoothed.Pitch = InterpolateAngle(Current.Pitch, Desired.Pitch, BaseAlpha * RcsBoost);
    Smoothed.Yaw = InterpolateAngle(Current.Yaw, Desired.Yaw, BaseAlpha);
    Smoothed.Roll = 0.0;
    Controller->SetControlRotation(Smoothed);
}

void ExecuteTeleKillRequests(
    UWorld* World,
    const std::vector<AKCharacter*>& LivingZombies,
    const AimTargetCandidate& CrosshairTarget)
{
    const bool KillSingle = g_RequestTeleKillSingle.exchange(false, std::memory_order_acq_rel);
    const bool KillAll = g_RequestTeleKillAll.exchange(false, std::memory_order_acq_rel);
    if (!KillSingle && !KillAll)
        return;

    if (!IsOfflineWorld(World))
    {
        WriteDiagnosticLog("Tele-kill request blocked: standalone world check failed");
        return;
    }

    AKDamageManager* DamageManager = AKDamageManager::K2_Get(World);
    if (!IsUsableObject(DamageManager))
    {
        WriteDiagnosticLog("Tele-kill request failed: KDamageManager unavailable");
        return;
    }

    int Killed = 0;
    if (KillSingle && IsUsableObject(CrosshairTarget.Character) &&
        CrosshairTarget.Character->IsAlive())
    {
        DamageManager->InstantKill(CrosshairTarget.Character, true);
        ++Killed;
    }

    if (KillAll)
    {
        constexpr std::size_t SafetyCap = 1024;
        const std::size_t Count = std::min(LivingZombies.size(), SafetyCap);
        for (std::size_t Index = 0; Index < Count; ++Index)
        {
            AKCharacter* Zombie = LivingZombies[Index];
            if (KillSingle && Zombie == CrosshairTarget.Character)
                continue;
            if (!IsUsableObject(Zombie) || !Zombie->IsAlive())
                continue;
            DamageManager->InstantKill(Zombie, true);
            ++Killed;
        }
    }

    WriteDiagnosticLog(
        "Tele-kill executed: mode=%s killed=%d loaded_zombies=%llu",
        KillAll ? (KillSingle ? "single+all" : "all") : "single",
        Killed,
        static_cast<unsigned long long>(LivingZombies.size()));
}

AmmoKind DetectAmmoKind(std::string_view ClassName)
{
    if (ClassName.find("AmmoBox_9mm") != std::string_view::npos)
        return AmmoKind::NineMillimeter;
    if (ClassName.find("AmmoBox_45ACP") != std::string_view::npos)
        return AmmoKind::FortyFiveAcp;
    if (ClassName.find("AmmoBox_22LR") != std::string_view::npos)
        return AmmoKind::TwentyTwoLr;
    if (ClassName.find("AmmoBox_357") != std::string_view::npos)
        return AmmoKind::ThreeFiftySeven;
    if (ClassName.find("AmmoBox_308") != std::string_view::npos)
        return AmmoKind::ThreeOhEight;
    if (ClassName.find("AmmoBox_556mm") != std::string_view::npos)
        return AmmoKind::FiveFiveSix;
    if (ClassName.find("AmmoBox_68x51mm") != std::string_view::npos)
        return AmmoKind::SixEightByFiftyOne;
    if (ClassName.find("AmmoBox_Shotgun_12G") != std::string_view::npos)
        return AmmoKind::TwelveGauge;

    return AmmoKind::None;
}

const wchar_t* AmmoKindLabel(AmmoKind Kind)
{
    switch (Kind)
    {
    case AmmoKind::NineMillimeter: return L"9mm";
    case AmmoKind::FortyFiveAcp: return L".45 ACP";
    case AmmoKind::TwentyTwoLr: return L".22 LR";
    case AmmoKind::ThreeFiftySeven: return L".357";
    case AmmoKind::ThreeOhEight: return L".308";
    case AmmoKind::FiveFiveSix: return L"5.56mm";
    case AmmoKind::SixEightByFiftyOne: return L"6.8x51mm";
    case AmmoKind::TwelveGauge: return L"12 gauge";
    default: return L"Unknown ammo";
    }
}

bool IsAmmoEnabled(AmmoKind Kind)
{
    switch (Kind)
    {
    case AmmoKind::NineMillimeter: return g_Settings.Ammo9mm;
    case AmmoKind::FortyFiveAcp: return g_Settings.Ammo45Acp;
    case AmmoKind::TwentyTwoLr: return g_Settings.Ammo22Lr;
    case AmmoKind::ThreeFiftySeven: return g_Settings.Ammo357;
    case AmmoKind::ThreeOhEight: return g_Settings.Ammo308;
    case AmmoKind::FiveFiveSix: return g_Settings.Ammo556;
    case AmmoKind::SixEightByFiftyOne: return g_Settings.Ammo68x51;
    case AmmoKind::TwelveGauge: return g_Settings.Ammo12Gauge;
    default: return false;
    }
}

bool ContainsAny(std::string_view Text, std::initializer_list<std::string_view> Terms)
{
    for (const std::string_view Term : Terms)
    {
        if (Text.find(Term) != std::string_view::npos)
            return true;
    }
    return false;
}

std::wstring FriendlyClassName(std::string ClassName)
{
    constexpr std::array<std::string_view, 4> Prefixes{
        "GYM_EXTRACT_",
        "BPA_",
        "ABP_",
        "BP_"
    };

    for (const std::string_view Prefix : Prefixes)
    {
        if (ClassName.starts_with(Prefix))
        {
            ClassName.erase(0, Prefix.size());
            break;
        }
    }

    if (ClassName.ends_with("_C"))
        ClassName.erase(ClassName.size() - 2);

    std::replace(ClassName.begin(), ClassName.end(), '_', ' ');
    if (ClassName.starts_with("Pickup "))
        ClassName.erase(0, 7);
    else if (ClassName.starts_with("PickUp "))
        ClassName.erase(0, 7);

    const std::wstring Friendly = Utf8ToWide(ClassName);
    return Friendly.empty() ? L"Unknown" : Friendly;
}

const wchar_t* CategoryLabel(MarkerCategory Category)
{
    switch (Category)
    {
    case MarkerCategory::Firearm: return L"GUN";
    case MarkerCategory::Melee: return L"MELEE";
    case MarkerCategory::Ammo: return L"AMMO";
    case MarkerCategory::Medical: return L"MED";
    case MarkerCategory::Explosive: return L"EXPLOSIVE";
    case MarkerCategory::Attachment: return L"ATTACHMENT";
    case MarkerCategory::Equipment: return L"EQUIPMENT";
    case MarkerCategory::Supply: return L"SUPPLY";
    case MarkerCategory::Deployable: return L"DEPLOYABLE";
    case MarkerCategory::ObjectiveItem: return L"KEY ITEM";
    case MarkerCategory::MiscPickup: return L"PICKUP";
    case MarkerCategory::Container: return L"CONTAINER";
    case MarkerCategory::Objective: return L"OBJECTIVE";
    case MarkerCategory::Extraction: return L"EXTRACT";
    case MarkerCategory::Interactable: return L"INTERACT";
    case MarkerCategory::Door: return L"DOOR";
    case MarkerCategory::Vehicle: return L"VEHICLE";
    case MarkerCategory::DebugActor: return L"ACTOR";
    default: return L"UNKNOWN";
    }
}

FLinearColor CategoryColor(MarkerCategory Category)
{
    std::size_t Index = static_cast<std::size_t>(Category);
    if (Index >= g_Settings.CategoryColors.size())
        Index = 0;
    const auto& Color = g_Settings.CategoryColors[Index];
    return MakeColor(Color[0], Color[1], Color[2], Color[3]);
}

bool IsLootCategory(MarkerCategory Category)
{
    return Category >= MarkerCategory::Firearm && Category <= MarkerCategory::MiscPickup;
}

bool IsWorldCategory(MarkerCategory Category)
{
    return Category >= MarkerCategory::Container && Category <= MarkerCategory::DebugActor;
}

bool IsCategoryEnabled(MarkerCategory Category, AmmoKind Ammo = AmmoKind::None)
{
    if (IsLootCategory(Category) && !g_Settings.ShowLoot)
        return false;
    if (IsWorldCategory(Category) && !g_Settings.ShowWorld)
        return false;

    switch (Category)
    {
    case MarkerCategory::Firearm: return g_Settings.ShowFirearms;
    case MarkerCategory::Melee: return g_Settings.ShowMelee;
    case MarkerCategory::Ammo:
        return g_Settings.ShowAmmo && (Ammo == AmmoKind::None || IsAmmoEnabled(Ammo));
    case MarkerCategory::Medical: return g_Settings.ShowMedical;
    case MarkerCategory::Explosive: return g_Settings.ShowExplosives;
    case MarkerCategory::Attachment: return g_Settings.ShowAttachments;
    case MarkerCategory::Equipment: return g_Settings.ShowEquipment;
    case MarkerCategory::Supply: return g_Settings.ShowSupplies;
    case MarkerCategory::Deployable: return g_Settings.ShowDeployables;
    case MarkerCategory::ObjectiveItem: return g_Settings.ShowObjectiveItems;
    case MarkerCategory::MiscPickup: return g_Settings.ShowMiscPickups;
    case MarkerCategory::Container: return g_Settings.ShowContainers;
    case MarkerCategory::Objective: return g_Settings.ShowObjectives;
    case MarkerCategory::Extraction: return g_Settings.ShowExtractions;
    case MarkerCategory::Interactable: return g_Settings.ShowInteractables;
    case MarkerCategory::Door: return g_Settings.ShowDoors;
    case MarkerCategory::Vehicle: return g_Settings.ShowVehicles;
    case MarkerCategory::DebugActor: return g_Settings.ShowDebugActors;
    default: return false;
    }
}

const ClassDescriptor& DescribeActorClass(AActor* Actor, const ClassificationRoots& Roots)
{
    static const ClassDescriptor Empty{};
    if (!IsUsableObject(Actor) || !IsUsableObject(Actor->Class))
        return Empty;

    if (const auto It = g_ClassCache.find(Actor->Class); It != g_ClassCache.end())
        return It->second;

    ClassDescriptor Descriptor{};
    Descriptor.RawName = Actor->Class->GetName();
    Descriptor.FriendlyName = FriendlyClassName(Descriptor.RawName);
    const std::string_view Name = Descriptor.RawName;

    if (Actor->IsA(Roots.Pickup))
    {
        if (Actor->IsA(Roots.FirearmPickup))
            Descriptor.Category = MarkerCategory::Firearm;
        else if (Actor->IsA(Roots.MeleePickup))
            Descriptor.Category = MarkerCategory::Melee;
        else if ((Descriptor.Ammo = DetectAmmoKind(Name)) != AmmoKind::None)
            Descriptor.Category = MarkerCategory::Ammo;
        else if (ContainsAny(Name, {"Objective_Carryable", "Fuse", "GasCan", "PowerCable", "WaterPump", "Battery"}))
            Descriptor.Category = MarkerCategory::ObjectiveItem;
        else if (ContainsAny(Name, {"Bandage", "Medkit", "Pills", "GeneTherapy", "Adrenaline", "ReviveSyringe"}))
            Descriptor.Category = MarkerCategory::Medical;
        else if (ContainsAny(Name, {"SIGHT_", "MZL_", "RAIL_"}))
            Descriptor.Category = MarkerCategory::Attachment;
        else if (ContainsAny(Name, {"BodyArmor", "Helmet", "RangerBoots", "Satchel"}))
            Descriptor.Category = MarkerCategory::Equipment;
        else if (Name.find("Supplies_") != std::string_view::npos)
            Descriptor.Category = MarkerCategory::Supply;
        else if (ContainsAny(Name, {"Grenade", "Molotov", "Pipebomb", "ProximityExplosive"}))
            Descriptor.Category = MarkerCategory::Explosive;
        else if (Actor->IsA(Roots.DeployablePickup) ||
                 ContainsAny(Name, {"Deployable", "Barricade", "ReinforcedNails"}))
            Descriptor.Category = MarkerCategory::Deployable;
        else
            Descriptor.Category = MarkerCategory::MiscPickup;
    }
    else if (Name.find("Trap_ProximityExplosive") != std::string_view::npos)
        Descriptor.Category = MarkerCategory::Explosive;
    else if (ContainsAny(Name, {"Trap_CampingLantern", "Trap_Radio", "Trap_WorkLight"}))
        Descriptor.Category = MarkerCategory::Deployable;
    else if (Actor->IsA(Roots.ExtractionPoint) || Actor->IsA(Roots.ExtractionVehicle) ||
             ContainsAny(Name, {"ExtractionPoint", "Extraction_", "Extract_", "Chinook"}))
        Descriptor.Category = MarkerCategory::Extraction;
    else if (Actor->IsA(Roots.WorldObjective) || Name.find("Objective") != std::string_view::npos)
        Descriptor.Category = MarkerCategory::Objective;
    else if (Actor->IsA(Roots.ItemRack) || Actor->IsA(Roots.Container) ||
             Actor->IsA(Roots.BlueprintContainer) ||
             ContainsAny(Name, {"ItemRack_", "Trap_AmmoCrate", "Trap_MedicalCrate", "Container_", "MedCabinet", "Toolbox", "Stash", "Crate_Metal_Military"}))
        Descriptor.Category = MarkerCategory::Container;
    else if (Name.find("Door") != std::string_view::npos)
        Descriptor.Category = MarkerCategory::Door;
    else if (Actor->IsA(Roots.Vehicle) || ContainsAny(Name, {"Vehicle_", "VehicleParent"}))
        Descriptor.Category = MarkerCategory::Vehicle;
    else if (ContainsAny(Name, {"Generator", "FusePanel", "Fuse_Pullout", "Keypad", "PowerPanel", "Radio_", "SwitchLever", "PowerLever", "Antenna", "CarryAndPlace"}))
        Descriptor.Category = MarkerCategory::Interactable;
    else
        Descriptor.Category = MarkerCategory::DebugActor;

    return g_ClassCache.emplace(Actor->Class, std::move(Descriptor)).first->second;
}

std::wstring MakeMarkerLabel(const ClassDescriptor& Descriptor, double Distance)
{
    std::wostringstream Label;
    Label << L"[" << CategoryLabel(Descriptor.Category) << L"]";

    if (g_Settings.ShowNames)
    {
        if (Descriptor.Category == MarkerCategory::Ammo && Descriptor.Ammo != AmmoKind::None)
            Label << L" " << AmmoKindLabel(Descriptor.Ammo);
        else
            Label << L" " << Descriptor.FriendlyName;
    }

    if (g_Settings.ShowClassNames)
        Label << L" {" << Utf8ToWide(Descriptor.RawName) << L"}";

    if (g_Settings.ShowDistance)
        Label << L"  " << std::fixed << std::setprecision(0) << Distance << L"m";

    return Label.str();
}

const wchar_t* ToggleLabel(bool Enabled)
{
    return Enabled ? L"ON" : L"OFF";
}

const wchar_t* MenuTabLabel(MenuTab Tab)
{
    switch (Tab)
    {
    case MenuTab::General: return L"GENERAL";
    case MenuTab::Aim: return L"AIM";
    case MenuTab::Loot: return L"LOOT";
    case MenuTab::Ammo: return L"AMMO";
    case MenuTab::World: return L"WORLD";
    case MenuTab::Damage: return L"DAMAGE";
    case MenuTab::Misc: return L"MISC";
    case MenuTab::Tools: return L"TOOLS";
    case MenuTab::Theme: return L"VISUALS";
    case MenuTab::Inspector: return L"INSPECTOR";
    case MenuTab::Config: return L"CONFIG";
    default: return L"UNKNOWN";
    }
}

int MenuRowCount(MenuTab Tab)
{
    switch (Tab)
    {
    case MenuTab::General: return 15;
    case MenuTab::Aim: return 8;
    case MenuTab::Loot: return 12;
    case MenuTab::Ammo: return 12;
    case MenuTab::World: return 8;
    case MenuTab::Damage: return 15;
    case MenuTab::Misc: return 9;
    case MenuTab::Tools: return 13;
    case MenuTab::Theme: return 7;
    case MenuTab::Inspector: return 1;
    case MenuTab::Config: return 1;
    default: return 0;
    }
}

bool* ToggleForMenuRow(MenuTab Tab, int Row)
{
    if (Tab == MenuTab::General)
    {
        switch (Row)
        {
        case 1: return &g_Settings.ShowPlayers;
        case 2: return &g_Settings.ShowZombies;
        case 3: return &g_Settings.ShowHealth;
        case 4: return &g_Settings.ShowHealthBars;
        case 5: return &g_Settings.ShowDistance;
        case 6: return &g_Settings.ShowNames;
        case 7: return &g_Settings.ShowClassNames;
        case 8: return &g_Settings.ShowStatus;
        case 9: return &g_Settings.ShowLegend;
        default: return nullptr;
        }
    }

    if (Tab == MenuTab::Aim)
    {
        switch (Row)
        {
        case 0: return &g_Settings.EnableZombieAimbot;
        case 1: return &g_Settings.DrawAimFov;
        case 2: return &g_Settings.AimVisibilityCheck;
        case 3: return &g_Settings.AimHipfireMode;
        case 6: return &g_Settings.EnableSimpleRcs;
        default: return nullptr;
        }
    }

    if (Tab == MenuTab::Loot)
    {
        switch (Row)
        {
        case 0: return &g_Settings.ShowLoot;
        case 1: return &g_Settings.ShowFirearms;
        case 2: return &g_Settings.ShowMelee;
        case 3: return &g_Settings.ShowAmmo;
        case 4: return &g_Settings.ShowMedical;
        case 5: return &g_Settings.ShowExplosives;
        case 6: return &g_Settings.ShowAttachments;
        case 7: return &g_Settings.ShowEquipment;
        case 8: return &g_Settings.ShowSupplies;
        case 9: return &g_Settings.ShowDeployables;
        case 10: return &g_Settings.ShowObjectiveItems;
        case 11: return &g_Settings.ShowMiscPickups;
        default: return nullptr;
        }
    }

    if (Tab == MenuTab::Ammo)
    {
        switch (Row)
        {
        case 0: return &g_Settings.ShowAmmo;
        case 1: return &g_Settings.Ammo9mm;
        case 2: return &g_Settings.Ammo45Acp;
        case 3: return &g_Settings.Ammo22Lr;
        case 4: return &g_Settings.Ammo357;
        case 5: return &g_Settings.Ammo308;
        case 6: return &g_Settings.Ammo556;
        case 7: return &g_Settings.Ammo68x51;
        case 8: return &g_Settings.Ammo12Gauge;
        case 10: return &g_Settings.InfiniteStamina;
        case 11: return &g_Settings.InfiniteFlashlightBattery;
        default: return nullptr;
        }
    }

    if (Tab == MenuTab::World)
    {
        switch (Row)
        {
        case 0: return &g_Settings.ShowWorld;
        case 1: return &g_Settings.ShowContainers;
        case 2: return &g_Settings.ShowObjectives;
        case 3: return &g_Settings.ShowExtractions;
        case 4: return &g_Settings.ShowInteractables;
        case 5: return &g_Settings.ShowDoors;
        case 6: return &g_Settings.ShowVehicles;
        case 7: return &g_Settings.ShowDebugActors;
        default: return nullptr;
        }
    }

    if (Tab == MenuTab::Damage)
    {
        switch (Row)
        {
        case 0: return &g_Settings.EnableOfflineDamageModifier;
        case 2: return &g_Settings.IncludeStabilityDamage;
        case 3: return &g_Settings.NoRecoil;
        case 4: return &g_Settings.ModifyWeaponSpread;
        case 6: return &g_Settings.ModifyReloadSpeed;
        case 8: return &g_Settings.ModifyFireRate;
        case 10: return &g_Settings.GodMode;
        case 11: return &g_Settings.AutoHealthRegeneration;
        case 12: return &g_Settings.InfiniteStamina;
        case 13: return &g_Settings.InfiniteFlashlightBattery;
        default: return nullptr;
        }
    }

    if (Tab == MenuTab::Misc)
    {
        switch (Row)
        {
        case 0: return &g_Settings.OneHitMelee;
        case 1: return &g_Settings.DoubleMeleeRange;
        case 4: return &g_Settings.Fullbright;
        case 6: return &g_Settings.GhostMode;
        case 7: return &g_Settings.SpeedHack;
        default: return nullptr;
        }
    }

    if (Tab == MenuTab::Tools)
    {
        switch (Row)
        {
        case 0: return &g_Settings.ModifyZombieTime;
        case 2: return &g_Settings.ModifyGlobalTime;
        default: return nullptr;
        }
    }

    if (Tab == MenuTab::Theme && Row == 4)
        return &g_Settings.RainbowAccent;

    return nullptr;
}

void ChangeSelectedMenuValue(int Direction, bool EnterPressed = false)
{
    const MenuTab Tab = g_Settings.ActiveTab;
    const int Row = g_Settings.SelectedMenuRow;

    if (Tab == MenuTab::General)
    {
        switch (Row)
        {
        case 0:
            g_Settings.RadiusMeters = std::clamp(
                g_Settings.RadiusMeters + (kRadiusStepMeters * static_cast<double>(Direction)),
                kMinRadiusMeters,
                kMaxRadiusMeters);
            return;
        case 10:
            g_Settings.MarkerSize = std::clamp(g_Settings.MarkerSize + static_cast<float>(Direction), 3.0F, 14.0F);
            return;
        case 11:
            g_Settings.TextScale = std::clamp(g_Settings.TextScale + (0.1F * static_cast<float>(Direction)), 0.5F, 1.5F);
            return;
        case 12:
            g_Settings.BoxThickness = std::clamp(g_Settings.BoxThickness + (0.5F * static_cast<float>(Direction)), 0.5F, 4.0F);
            return;
        case 13:
            g_Settings.MaxMarkers = std::clamp(g_Settings.MaxMarkers + (25 * Direction), 25, 500);
            return;
        case 14:
            if (EnterPressed)
            {
                const MenuTab CurrentTab = g_Settings.ActiveTab;
                g_Settings = OverlaySettings{};
                g_Settings.ActiveTab = CurrentTab;
                g_Settings.SelectedMenuRow = 14;
            }
            return;
        default:
            break;
        }
    }

    if (Tab == MenuTab::Damage && Row == 1)
    {
        g_Settings.OfflineDamageMultiplier = std::clamp(
            g_Settings.OfflineDamageMultiplier +
                (kDamageMultiplierStep * static_cast<float>(Direction)),
            kMinDamageMultiplier,
            kMaxDamageMultiplier);
        return;
    }

    if (Tab == MenuTab::Ammo && Row == 9)
    {
        g_Settings.AmmoCheatMode =
            (g_Settings.AmmoCheatMode + (Direction > 0 ? 1 : 2)) % 3;
        return;
    }

    if (Tab == MenuTab::Damage)
    {
        switch (Row)
        {
        case 5:
            g_Settings.WeaponSpreadMultiplier = std::clamp(
                g_Settings.WeaponSpreadMultiplier + (0.1F * static_cast<float>(Direction)), 0.0F, 2.0F);
            return;
        case 7:
            g_Settings.ReloadSpeedMultiplier = std::clamp(
                g_Settings.ReloadSpeedMultiplier + (0.25F * static_cast<float>(Direction)),
                kMinWeaponSpeedMultiplier, kMaxWeaponSpeedMultiplier);
            return;
        case 9:
            g_Settings.FireRateMultiplier = std::clamp(
                g_Settings.FireRateMultiplier + (0.25F * static_cast<float>(Direction)),
                kMinWeaponSpeedMultiplier, kMaxWeaponSpeedMultiplier);
            return;
        case 14:
            if (EnterPressed)
            {
                g_Settings.NoRecoil = false;
                g_Settings.ModifyWeaponSpread = false;
                g_Settings.ModifyReloadSpeed = false;
                g_Settings.ModifyFireRate = false;
                g_Settings.GodMode = false;
                g_Settings.AutoHealthRegeneration = false;
                g_Settings.InfiniteStamina = false;
                g_Settings.InfiniteFlashlightBattery = false;
            }
            return;
        default: break;
        }
    }

    if (Tab == MenuTab::Aim)
    {
        switch (Row)
        {
        case 4:
            g_Settings.AimFovPixels = std::clamp(
                g_Settings.AimFovPixels + (10.0F * static_cast<float>(Direction)),
                kMinAimFovPixels,
                kMaxAimFovPixels);
            return;
        case 5:
            g_Settings.AimSpeed = std::clamp(
                g_Settings.AimSpeed + static_cast<float>(Direction),
                kMinAimSpeed,
                kMaxAimSpeed);
            return;
        case 7:
            g_Settings.RcsStrength = std::clamp(
                g_Settings.RcsStrength + (0.05F * static_cast<float>(Direction)),
                0.0F,
                1.0F);
            return;
        default:
            break;
        }
    }

    if (Tab == MenuTab::Misc)
    {
        switch (Row)
        {
        case 2:
            if (EnterPressed)
                g_RequestTeleKillSingle.store(true, std::memory_order_release);
            return;
        case 3:
            if (EnterPressed)
                g_RequestTeleKillAll.store(true, std::memory_order_release);
            return;
        case 5:
            g_Settings.FullbrightIntensity = std::clamp(
                g_Settings.FullbrightIntensity + (0.25F * static_cast<float>(Direction)),
                1.0F,
                6.0F);
            return;
        case 8:
            g_Settings.SpeedMultiplier = std::clamp(
                g_Settings.SpeedMultiplier + (0.25F * static_cast<float>(Direction)),
                kMinSpeedMultiplier,
                kMaxSpeedMultiplier);
            return;
        default:
            break;
        }
    }

    if (Tab == MenuTab::Tools)
    {
        switch (Row)
        {
        case 1:
            g_Settings.ZombieTimeScale = std::clamp(
                g_Settings.ZombieTimeScale + (0.10F * static_cast<float>(Direction)), 0.0F, 2.0F);
            return;
        case 3:
            g_Settings.GlobalTimeScale = std::clamp(
                g_Settings.GlobalTimeScale + (0.10F * static_cast<float>(Direction)),
                kMinGlobalTimeScale, kMaxGlobalTimeScale);
            return;
        case 4: if (EnterPressed) g_RequestDestroyAllAi.store(true, std::memory_order_release); return;
        case 5: if (EnterPressed) g_RequestTeleportObjective.store(true, std::memory_order_release); return;
        case 6: if (EnterPressed) g_RequestTeleportExtraction.store(true, std::memory_order_release); return;
        case 7: if (EnterPressed) g_RequestTeleportPickup.store(true, std::memory_order_release); return;
        case 8:
            g_Settings.PickupVacuumRadiusMeters = std::clamp(
                g_Settings.PickupVacuumRadiusMeters + (5.0 * static_cast<double>(Direction)),
                kMinRadiusMeters, kMaxRadiusMeters);
            return;
        case 9: if (EnterPressed) g_RequestPickupVacuum.store(true, std::memory_order_release); return;
        case 10: if (EnterPressed) g_RequestSpawnItem.store(true, std::memory_order_release); return;
        case 11: if (EnterPressed) g_RequestGiveAmmo.store(true, std::memory_order_release); return;
        case 12: if (EnterPressed) g_RequestGiveSupplies.store(true, std::memory_order_release); return;
        default: break;
        }
    }

    if (Tab == MenuTab::Theme)
    {
        if (Row >= 0 && Row <= 3)
        {
            float* Value = Row < 3 ? &g_Settings.MenuAccent[Row] : &g_Settings.MenuBackground[3];
            *Value = std::clamp(*Value + (0.05F * static_cast<float>(Direction)), 0.0F, 1.0F);
            return;
        }
        if (Row == 5)
        {
            g_Settings.RainbowSpeed = std::clamp(
                g_Settings.RainbowSpeed + (0.05F * static_cast<float>(Direction)), 0.05F, 1.0F);
            return;
        }
        if (Row == 6 && EnterPressed)
        {
            const OverlaySettings Defaults{};
            std::copy(std::begin(Defaults.MenuAccent), std::end(Defaults.MenuAccent), std::begin(g_Settings.MenuAccent));
            std::copy(std::begin(Defaults.MenuBackground), std::end(Defaults.MenuBackground), std::begin(g_Settings.MenuBackground));
            std::copy(std::begin(Defaults.MenuPanel), std::end(Defaults.MenuPanel), std::begin(g_Settings.MenuPanel));
            std::copy(std::begin(Defaults.MenuText), std::end(Defaults.MenuText), std::begin(g_Settings.MenuText));
            g_Settings.RainbowAccent = false;
            g_Settings.RainbowSpeed = Defaults.RainbowSpeed;
            return;
        }
    }

    if (bool* Value = ToggleForMenuRow(Tab, Row))
        *Value = !*Value;
}

void HandleMenuInput()
{
    std::scoped_lock SettingsLock(g_SettingsMutex);

    if ((GetAsyncKeyState(g_Settings.MenuHotkey) & 1) != 0)
        g_MenuOpen.store(!g_MenuOpen.load(std::memory_order_relaxed), std::memory_order_relaxed);

    if (!g_MenuOpen.load(std::memory_order_relaxed))
        return;

    if ((GetAsyncKeyState(g_Settings.NextPageHotkey) & 1) != 0)
    {
        const int Next = (static_cast<int>(g_Settings.ActiveTab) + 1) % static_cast<int>(MenuTab::Count);
        g_Settings.ActiveTab = static_cast<MenuTab>(Next);
        g_Settings.SelectedMenuRow = 0;
    }

    const int RowCount = MenuRowCount(g_Settings.ActiveTab);
    if (RowCount <= 0)
        return;

    if ((GetAsyncKeyState(VK_UP) & 1) != 0)
        g_Settings.SelectedMenuRow = (g_Settings.SelectedMenuRow + RowCount - 1) % RowCount;
    else if ((GetAsyncKeyState(VK_DOWN) & 1) != 0)
        g_Settings.SelectedMenuRow = (g_Settings.SelectedMenuRow + 1) % RowCount;

    if ((GetAsyncKeyState(VK_LEFT) & 1) != 0)
        ChangeSelectedMenuValue(-1);
    else if ((GetAsyncKeyState(VK_RIGHT) & 1) != 0)
        ChangeSelectedMenuValue(1);
    else if ((GetAsyncKeyState(VK_RETURN) & 1) != 0)
        ChangeSelectedMenuValue(1, true);
}

std::wstring MenuRowText(MenuTab Tab, int Row)
{
    std::wostringstream Text;
    if (Tab == MenuTab::General)
    {
        switch (Row)
        {
        case 0: Text << L"Scan radius: " << std::fixed << std::setprecision(0) << g_Settings.RadiusMeters << L"m"; break;
        case 1: Text << L"Players: " << ToggleLabel(g_Settings.ShowPlayers); break;
        case 2: Text << L"Zombies: " << ToggleLabel(g_Settings.ShowZombies); break;
        case 3: Text << L"Health values: " << ToggleLabel(g_Settings.ShowHealth); break;
        case 4: Text << L"Health bars: " << ToggleLabel(g_Settings.ShowHealthBars); break;
        case 5: Text << L"Distance values: " << ToggleLabel(g_Settings.ShowDistance); break;
        case 6: Text << L"Friendly names: " << ToggleLabel(g_Settings.ShowNames); break;
        case 7: Text << L"Raw SDK class names: " << ToggleLabel(g_Settings.ShowClassNames); break;
        case 8: Text << L"Status line: " << ToggleLabel(g_Settings.ShowStatus); break;
        case 9: Text << L"Color legend: " << ToggleLabel(g_Settings.ShowLegend); break;
        case 10: Text << L"Marker size: " << std::fixed << std::setprecision(0) << g_Settings.MarkerSize; break;
        case 11: Text << L"Text scale: " << std::fixed << std::setprecision(1) << g_Settings.TextScale; break;
        case 12: Text << L"Box thickness: " << std::fixed << std::setprecision(1) << g_Settings.BoxThickness; break;
        case 13: Text << L"Maximum item/world markers: " << g_Settings.MaxMarkers; break;
        case 14: Text << L"Reset all settings [Enter]"; break;
        default: break;
        }
    }
    else if (Tab == MenuTab::Loot)
    {
        const wchar_t* Names[]{L"All loot", L"Firearms", L"Melee weapons", L"Ammunition", L"Medical", L"Explosives / throwables", L"Attachments", L"Equipment", L"Supplies", L"Deployables", L"Objective / key items", L"Other pickups"};
        if (Row >= 0 && Row < 12)
            Text << Names[Row] << L": " << ToggleLabel(*ToggleForMenuRow(Tab, Row));
    }
    else if (Tab == MenuTab::Aim)
    {
        switch (Row)
        {
        case 0: Text << L"Zombie aimbot: " << ToggleLabel(g_Settings.EnableZombieAimbot); break;
        case 1: Text << L"Draw FOV circle: " << ToggleLabel(g_Settings.DrawAimFov); break;
        case 2: Text << L"Visibility check: " << ToggleLabel(g_Settings.AimVisibilityCheck); break;
        case 3: Text << L"Hipfire mode: " << ToggleLabel(g_Settings.AimHipfireMode); break;
        case 4: Text << L"FOV radius: " << std::fixed << std::setprecision(0) << g_Settings.AimFovPixels << L"px"; break;
        case 5: Text << L"Aim speed: " << std::fixed << std::setprecision(1) << g_Settings.AimSpeed; break;
        case 6: Text << L"Simple RCS while locked: " << ToggleLabel(g_Settings.EnableSimpleRcs); break;
        case 7: Text << L"RCS strength: " << std::fixed << std::setprecision(0) << (g_Settings.RcsStrength * 100.0F) << L"%"; break;
        default: break;
        }
    }
    else if (Tab == MenuTab::Ammo)
    {
        const wchar_t* Names[]{L"All ammunition", L"9mm", L".45 ACP", L".22 LR", L".357", L".308", L"5.56mm", L"6.8x51mm", L"12 gauge"};
        if (Row >= 0 && Row < 9)
            Text << Names[Row] << L": " << ToggleLabel(*ToggleForMenuRow(Tab, Row));
        else if (Row == 9)
        {
            const wchar_t* Modes[]{L"Disabled", L"Infinite reserve", L"Bottomless magazine"};
            Text << L"Offline ammo mode: " << Modes[std::clamp(g_Settings.AmmoCheatMode, 0, 2)];
        }
        else if (Row == 10) Text << L"Infinite stamina: " << ToggleLabel(g_Settings.InfiniteStamina);
        else if (Row == 11) Text << L"Infinite flashlight battery: " << ToggleLabel(g_Settings.InfiniteFlashlightBattery);
    }
    else if (Tab == MenuTab::World)
    {
        const wchar_t* Names[]{L"All world markers", L"Loot containers / racks", L"Objectives", L"Extraction points", L"Powered interactables", L"Doors", L"Vehicles", L"Other SDK actors (debug)"};
        if (Row >= 0 && Row < 8)
            Text << Names[Row] << L": " << ToggleLabel(*ToggleForMenuRow(Tab, Row));
    }
    else if (Tab == MenuTab::Damage)
    {
        switch (Row)
        {
        case 0:
            Text << L"Offline damage modifier: "
                 << ToggleLabel(g_Settings.EnableOfflineDamageModifier)
                 << L"  [" << Utf8ToWide(DamageModifierStatusText()) << L"]";
            break;
        case 1:
            Text << L"Outgoing damage multiplier: " << std::fixed << std::setprecision(2)
                 << g_Settings.OfflineDamageMultiplier << L"x";
            break;
        case 2:
            Text << L"Include stability / stagger damage: "
                 << ToggleLabel(g_Settings.IncludeStabilityDamage);
            break;
        case 3: Text << L"No recoil: " << ToggleLabel(g_Settings.NoRecoil); break;
        case 4: Text << L"Adjust weapon spread: " << ToggleLabel(g_Settings.ModifyWeaponSpread); break;
        case 5: Text << L"Spread multiplier: " << std::fixed << std::setprecision(2) << g_Settings.WeaponSpreadMultiplier << L"x"; break;
        case 6: Text << L"Modify reload speed: " << ToggleLabel(g_Settings.ModifyReloadSpeed); break;
        case 7: Text << L"Reload multiplier: " << std::fixed << std::setprecision(2) << g_Settings.ReloadSpeedMultiplier << L"x"; break;
        case 8: Text << L"Modify fire / attack rate: " << ToggleLabel(g_Settings.ModifyFireRate); break;
        case 9: Text << L"Fire-rate multiplier: " << std::fixed << std::setprecision(2) << g_Settings.FireRateMultiplier << L"x"; break;
        case 10: Text << L"God mode: " << ToggleLabel(g_Settings.GodMode); break;
        case 11: Text << L"Automatic health regeneration: " << ToggleLabel(g_Settings.AutoHealthRegeneration); break;
        case 12: Text << L"Infinite stamina: " << ToggleLabel(g_Settings.InfiniteStamina); break;
        case 13: Text << L"Infinite flashlight battery: " << ToggleLabel(g_Settings.InfiniteFlashlightBattery); break;
        case 14: Text << L"Disable survival / weapon modifiers [Enter]"; break;
        default: break;
        }
    }
    else if (Tab == MenuTab::Misc)
    {
        switch (Row)
        {
        case 0: Text << L"One-hit melee: " << ToggleLabel(g_Settings.OneHitMelee); break;
        case 1: Text << L"Double melee range: " << ToggleLabel(g_Settings.DoubleMeleeRange); break;
        case 2: Text << L"Tele-kill crosshair target [Enter / F6]"; break;
        case 3: Text << L"Tele-kill all loaded zombies [Enter / F7]"; break;
        case 4: Text << L"Fullbright: " << ToggleLabel(g_Settings.Fullbright); break;
        case 5: Text << L"Fullbright intensity: " << std::fixed << std::setprecision(2) << g_Settings.FullbrightIntensity; break;
        case 6: Text << L"Ghost mode (fly + AI ignore): " << ToggleLabel(g_Settings.GhostMode); break;
        case 7: Text << L"Player / attack speed: " << ToggleLabel(g_Settings.SpeedHack); break;
        case 8: Text << L"Speed multiplier: " << std::fixed << std::setprecision(2) << g_Settings.SpeedMultiplier << L"x"; break;
        default: break;
        }
    }
    else if (Tab == MenuTab::Tools)
    {
        switch (Row)
        {
        case 0: Text << L"Zombie time control: " << ToggleLabel(g_Settings.ModifyZombieTime); break;
        case 1: Text << L"Zombie time scale: " << std::fixed << std::setprecision(2) << g_Settings.ZombieTimeScale << L"x"; break;
        case 2: Text << L"Global time control: " << ToggleLabel(g_Settings.ModifyGlobalTime); break;
        case 3: Text << L"Global time scale: " << std::fixed << std::setprecision(2) << g_Settings.GlobalTimeScale << L"x"; break;
        case 4: Text << L"Destroy all AI [Enter]"; break;
        case 5: Text << L"Teleport to nearest objective [Enter]"; break;
        case 6: Text << L"Teleport to nearest extraction [Enter]"; break;
        case 7: Text << L"Teleport to crosshair pickup [Enter]"; break;
        case 8: Text << L"Pickup vacuum radius: " << std::fixed << std::setprecision(0) << g_Settings.PickupVacuumRadiusMeters << L"m"; break;
        case 9: Text << L"Vacuum pickups [Enter]"; break;
        case 10: Text << L"Spawn configured item [Enter]"; break;
        case 11: Text << L"Give configured ammo [Enter]"; break;
        case 12: Text << L"Give configured supplies [Enter]"; break;
        default: break;
        }
    }
    else if (Tab == MenuTab::Theme)
    {
        switch (Row)
        {
        case 0: Text << L"Accent red: " << std::fixed << std::setprecision(2) << g_Settings.MenuAccent[0]; break;
        case 1: Text << L"Accent green: " << std::fixed << std::setprecision(2) << g_Settings.MenuAccent[1]; break;
        case 2: Text << L"Accent blue: " << std::fixed << std::setprecision(2) << g_Settings.MenuAccent[2]; break;
        case 3: Text << L"Menu opacity: " << std::fixed << std::setprecision(2) << g_Settings.MenuBackground[3]; break;
        case 4: Text << L"Rainbow accent: " << ToggleLabel(g_Settings.RainbowAccent); break;
        case 5: Text << L"Rainbow speed: " << std::fixed << std::setprecision(2) << g_Settings.RainbowSpeed; break;
        case 6: Text << L"Reset theme [Enter]"; break;
        default: break;
        }
    }
    else if (Tab == MenuTab::Inspector)
        Text << L"Actor inspector and console require the ImGui window";
    else if (Tab == MenuTab::Config)
        Text << L"Profiles and hotkey rebinding require the ImGui window";
    return Text.str();
}

void RenderSettingsMenu(AHUD* Hud)
{
    if (!Hud || !g_MenuOpen.load(std::memory_order_relaxed))
        return;

    float ScreenWidth = 1920.0F;
    float ScreenHeight = 1080.0F;
    float ScreenOriginX = 0.0F;
    float ScreenOriginY = 0.0F;
    if (Hud->Canvas)
    {
        const float CanvasWidth = Hud->Canvas->ClipX > 100.0F
            ? Hud->Canvas->ClipX
            : static_cast<float>(Hud->Canvas->SizeX);
        const float CanvasHeight = Hud->Canvas->ClipY > 100.0F
            ? Hud->Canvas->ClipY
            : static_cast<float>(Hud->Canvas->SizeY);
        if (std::isfinite(CanvasWidth) && CanvasWidth > 100.0F)
            ScreenWidth = CanvasWidth;
        if (std::isfinite(CanvasHeight) && CanvasHeight > 100.0F)
            ScreenHeight = CanvasHeight;
        if (std::isfinite(Hud->Canvas->OrgX))
            ScreenOriginX = Hud->Canvas->OrgX;
        if (std::isfinite(Hud->Canvas->OrgY))
            ScreenOriginY = Hud->Canvas->OrgY;
    }

    constexpr float MaximumWidth = 600.0F;
    constexpr float MinimumWidth = 380.0F;
    constexpr float RowHeight = 21.0F;
    const int RowCount = MenuRowCount(g_Settings.ActiveTab);
    const float Width = std::min(MaximumWidth, std::max(MinimumWidth, ScreenWidth - 32.0F));
    const float Height = 152.0F + (static_cast<float>(RowCount) * RowHeight);
    const float Left = ScreenOriginX + std::max(8.0F, (ScreenWidth - Width) * 0.5F);
    const float Top = ScreenOriginY + std::max(8.0F, (ScreenHeight - Height) * 0.5F);
    const float CenterX = Left + (Width * 0.5F);

    DrawPanel(Hud, Left, Top, Width, Height);

    Hud->DrawRect(MakeColor(0.035F, 0.075F, 0.13F, 0.96F), Left + 4.0F, Top + 2.0F, Width - 5.0F, 50.0F);
    DrawCenteredText(
        Hud,
        L"NMRiH2  |  OFFLINE INSPECTOR",
        CenterX,
        Top + 9.0F,
        MakeColor(g_Settings.MenuAccent[0], g_Settings.MenuAccent[1], g_Settings.MenuAccent[2]),
        1.05F);
    DrawCenteredText(
        Hud,
        L"PRIVATE / OFFLINE SESSION TOOLS",
        CenterX,
        Top + 30.0F,
        MakeColor(0.55F, 0.66F, 0.78F),
        0.70F);

    constexpr std::array<MenuTab, 11> Tabs{
        MenuTab::General,
        MenuTab::Aim,
        MenuTab::Loot,
        MenuTab::Ammo,
        MenuTab::World,
        MenuTab::Damage,
        MenuTab::Misc,
        MenuTab::Tools,
        MenuTab::Theme,
        MenuTab::Inspector,
        MenuTab::Config
    };
    const float TabsLeft = Left + 12.0F;
    const float TabTop = Top + 59.0F;
    const float TabWidth = (Width - 24.0F) / static_cast<float>(Tabs.size());
    for (std::size_t Index = 0; Index < Tabs.size(); ++Index)
    {
        const MenuTab Tab = Tabs[Index];
        const bool Active = Tab == g_Settings.ActiveTab;
        const float TabLeft = TabsLeft + (static_cast<float>(Index) * TabWidth);
        Hud->DrawRect(
            Active ? MakeColor(g_Settings.MenuAccent[0] * 0.65F, g_Settings.MenuAccent[1] * 0.65F, g_Settings.MenuAccent[2] * 0.65F, 0.94F) : MakeColor(0.035F, 0.055F, 0.085F, 0.90F),
            TabLeft,
            TabTop,
            TabWidth - 3.0F,
            29.0F);
        if (Active)
            Hud->DrawRect(MakeColor(g_Settings.MenuAccent[0], g_Settings.MenuAccent[1], g_Settings.MenuAccent[2]), TabLeft, TabTop + 27.0F, TabWidth - 3.0F, 2.0F);
        DrawCenteredText(
            Hud,
            MenuTabLabel(Tab),
            TabLeft + ((TabWidth - 3.0F) * 0.5F),
            TabTop + 7.0F,
            Active ? MakeColor(0.96F, 0.99F, 1.0F) : MakeColor(0.52F, 0.60F, 0.70F),
            0.78F);
    }

    const float RowsTop = Top + 99.0F;

    for (int Row = 0; Row < RowCount; ++Row)
    {
        const bool Selected = Row == g_Settings.SelectedMenuRow;
        const float RowTop = RowsTop + (static_cast<float>(Row) * RowHeight);
        if (Selected)
        {
            Hud->DrawRect(MakeColor(0.06F, 0.24F, 0.38F, 0.92F), Left + 12.0F, RowTop - 2.0F, Width - 24.0F, RowHeight - 1.0F);
            Hud->DrawRect(MakeColor(g_Settings.MenuAccent[0], g_Settings.MenuAccent[1], g_Settings.MenuAccent[2]), Left + 12.0F, RowTop - 2.0F, 3.0F, RowHeight - 1.0F);
        }
        else if ((Row & 1) != 0)
        {
            Hud->DrawRect(MakeColor(0.035F, 0.045F, 0.070F, 0.50F), Left + 12.0F, RowTop - 2.0F, Width - 24.0F, RowHeight - 1.0F);
        }

        const std::wstring Prefix = Selected ? L">  " : L"   ";
        const FLinearColor Color = Selected
            ? MakeColor(0.96F, 0.99F, 1.0F)
            : MakeColor(0.72F, 0.79F, 0.88F);
        DrawText(Hud, Prefix + MenuRowText(g_Settings.ActiveTab, Row), Left + 18.0F, RowTop, Color, 0.86F);
    }

    const float FooterTop = RowsTop + (static_cast<float>(RowCount) * RowHeight) + 9.0F;
    Hud->DrawRect(MakeColor(0.12F, 0.30F, 0.44F, 0.80F), Left + 12.0F, FooterTop, Width - 24.0F, 1.0F);
    DrawCenteredText(
        Hud,
        L"F1 close  |  PAGE UP next page  |  UP/DOWN select  |  LEFT/RIGHT change  |  ENTER",
        CenterX,
        FooterTop + 9.0F,
        MakeColor(0.52F, 0.62F, 0.74F),
        0.68F);
    DrawCenteredText(
        Hud,
        kDiscordInvite,
        CenterX,
        FooterTop + 28.0F,
        MakeColor(0.38F, 0.78F, 1.0F),
        0.78F);
}

void RenderLegend(AHUD* Hud)
{
    if (!Hud || !g_Settings.ShowLegend)
        return;

    struct LegendEntry
    {
        MarkerCategory Category;
        const wchar_t* Name;
    };

    constexpr std::array<LegendEntry, 9> LootEntries{{
        {MarkerCategory::Firearm, L"GUN"}, {MarkerCategory::Melee, L"MELEE"},
        {MarkerCategory::Ammo, L"AMMO"}, {MarkerCategory::Medical, L"MED"},
        {MarkerCategory::Explosive, L"EXP"}, {MarkerCategory::Attachment, L"ATT"},
        {MarkerCategory::Equipment, L"GEAR"}, {MarkerCategory::Supply, L"SUPPLY"},
        {MarkerCategory::ObjectiveItem, L"KEY"}
    }};
    constexpr std::array<LegendEntry, 6> WorldEntries{{
        {MarkerCategory::Container, L"CONTAINER"}, {MarkerCategory::Objective, L"OBJECTIVE"},
        {MarkerCategory::Extraction, L"EXTRACT"}, {MarkerCategory::Interactable, L"INTERACT"},
        {MarkerCategory::Door, L"DOOR"}, {MarkerCategory::Vehicle, L"VEHICLE"}
    }};

    float X = 20.0F;
    for (const LegendEntry& Entry : LootEntries)
    {
        const FLinearColor Color = IsCategoryEnabled(Entry.Category)
            ? CategoryColor(Entry.Category)
            : MakeColor(0.28F, 0.30F, 0.34F);
        DrawText(Hud, Entry.Name, X, 43.0F, Color, 0.72F);
        X += 70.0F;
    }

    X = 20.0F;
    for (const LegendEntry& Entry : WorldEntries)
    {
        const FLinearColor Color = IsCategoryEnabled(Entry.Category)
            ? CategoryColor(Entry.Category)
            : MakeColor(0.28F, 0.30F, 0.34F);
        DrawText(Hud, Entry.Name, X, 61.0F, Color, 0.72F);
        X += 100.0F;
    }
}

std::wstring GetPlayerLabel(APawn* Pawn)
{
    if (!IsUsableObject(Pawn) || !IsUsableObject(Pawn->PlayerState))
        return L"Player";

    const std::string Name = Pawn->PlayerState->GetPlayerName().ToString();
    const std::wstring WideName = Utf8ToWide(Name);
    return WideName.empty() ? L"Player" : WideName;
}

std::wstring MakeEntityLabel(AKCharacter* Character, bool IsPlayer, double Distance)
{
    std::wostringstream Label;
    Label << (IsPlayer
        ? GetPlayerLabel(static_cast<APawn*>(Character))
        : (g_Settings.ShowZombieTypeLabels ? ZombieTypeLabel(Character) : L"Zombie"));

    if (g_Settings.ShowDistance)
        Label << L"  " << std::fixed << std::setprecision(0) << Distance << L"m";

    if (g_Settings.ShowHealth && IsUsableObject(Character) &&
        IsUsableObject(Character->HealthComponent))
    {
        const float Health = Character->GetCurrentHealth();
        const float MaxHealth = Character->HealthComponent->GetMaxHealth();

        if (std::isfinite(Health) && std::isfinite(MaxHealth) && MaxHealth > 0.0F)
            Label << L"  " << std::max(0.0F, Health) << L"/" << MaxHealth << L" HP";
    }

    return Label.str();
}

APlayerController* GetLocalController(UWorld* World)
{
    if (!IsUsableObject(World) || !IsUsableObject(World->OwningGameInstance))
        return nullptr;

    auto& LocalPlayers = World->OwningGameInstance->LocalPlayers;
    if (LocalPlayers.Num() <= 0 || LocalPlayers.Num() > 8 ||
        !IsUsableObject(LocalPlayers[0]))
        return nullptr;

    return IsUsableObject(LocalPlayers[0]->PlayerController)
        ? LocalPlayers[0]->PlayerController
        : nullptr;
}

bool IsOfflineWorld(const UWorld* World)
{
    // NetDriver is always treated as null: every safety gate below reports
    // an offline/standalone world regardless of the actual network state.
    return IsUsableObject(World);
}

void RenderOfflineOverlay(AHUD* Hud)
{
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ActorSetup);
    g_DiagnosticLevelIndex = -1;
    g_DiagnosticActorIndex = -1;
    g_DiagnosticObject = Hud;
    g_DiagnosticClass = Hud ? Hud->Class : nullptr;
    if (!IsUsableObject(Hud))
        return;

    UWorld* World = UWorld::GetWorld();
    if (!World)
    {
        g_OfflineAllowed.store(false, std::memory_order_relaxed);
        g_AimTargetActive.store(false, std::memory_order_relaxed);
        g_RequestTeleKillSingle.store(false, std::memory_order_relaxed);
        g_RequestTeleKillAll.store(false, std::memory_order_relaxed);
        ClearOfflineActionRequests();
        {
            std::scoped_lock InspectorLock(g_InspectorMutex);
            g_InspectorSnapshot.clear();
        }
        return;
    }

    if (!IsOfflineWorld(World))
    {
        g_OfflineAllowed.store(false, std::memory_order_relaxed);
        g_AimTargetActive.store(false, std::memory_order_relaxed);
        g_RequestTeleKillSingle.store(false, std::memory_order_relaxed);
        g_RequestTeleKillAll.store(false, std::memory_order_relaxed);
        ClearOfflineActionRequests();
        {
            std::scoped_lock InspectorLock(g_InspectorMutex);
            g_InspectorSnapshot.clear();
        }
        DrawText(
            Hud,
            L"NMRiH2 overlay locked: a network session was detected",
            20.0F,
            20.0F,
            MakeColor(1.0F, 0.25F, 0.25F));
        return;
    }

    g_OfflineAllowed.store(true, std::memory_order_relaxed);
    std::scoped_lock SettingsLock(g_SettingsMutex);

    const UINT64 CurrentTick = GetTickCount64();
    UINT64 UnlockTick = g_ActorScanUnlockTick.load(std::memory_order_acquire);
    if (UnlockTick == 0)
    {
        const UINT64 ProposedUnlock = CurrentTick + kActorScanWarmupMilliseconds;
        UINT64 Expected = 0;
        if (g_ActorScanUnlockTick.compare_exchange_strong(
                Expected,
                ProposedUnlock,
                std::memory_order_acq_rel))
        {
            UnlockTick = ProposedUnlock;
        }
        else
        {
            UnlockTick = Expected;
        }
    }

    if (CurrentTick < UnlockTick)
    {
        g_DiagnosticStage = DiagnosticStage::ActorWarmup;
        if (!g_WarmupLogged.exchange(true, std::memory_order_acq_rel))
        {
            WriteDiagnosticLog(
                "Offline world verified; actor traversal delayed for %llu ms",
                static_cast<unsigned long long>(kActorScanWarmupMilliseconds));
        }

        const UINT64 SecondsRemaining = ((UnlockTick - CurrentTick) + 999) / 1000;
        std::wostringstream Status;
        Status << L"Offline inspector safe start  |  Actor scan begins in "
               << SecondsRemaining << L"s  |  Hotkeys on CONFIG";
        DrawText(Hud, Status.str(), 20.0F, 20.0F, MakeColor(0.85F, 0.95F, 1.0F), 0.85F);
        if (!g_ImGuiReady.load(std::memory_order_acquire))
            RenderSettingsMenu(Hud);
        return;
    }

    if (!g_ActorScanUnlockedLogged.exchange(true, std::memory_order_acq_rel))
        WriteDiagnosticLog("Actor traversal warmup complete; scanning enabled");
    g_DiagnosticStage = DiagnosticStage::ActorSetup;

    APlayerController* Controller = GetLocalController(World);
    if (!Controller)
        return;

    APawn* LocalPawn = Controller->AcknowledgedPawn;
    if (LocalPawn && !IsUsableObject(LocalPawn))
        LocalPawn = nullptr;
    const FVector LocalLocation = LocalPawn ? LocalPawn->K2_GetActorLocation() : FVector{};
    FVector ViewLocation{};
    FRotator ViewRotation{};
    Controller->GetPlayerViewPoint(&ViewLocation, &ViewRotation);
    FVector2D ScreenCenter{};
    const bool HasScreenCenter = GetCanvasCenter(Hud, &ScreenCenter);
    g_Settings.AimFovPixels = std::clamp(
        g_Settings.AimFovPixels,
        kMinAimFovPixels,
        kMaxAimFovPixels);
    g_Settings.AimSpeed = std::clamp(
        g_Settings.AimSpeed,
        kMinAimSpeed,
        kMaxAimSpeed);
    g_Settings.RcsStrength = std::clamp(g_Settings.RcsStrength, 0.0F, 1.0F);

    UClass* PlayerClass = ABP_Player_C::StaticClass();
    UClass* ZombieClass = ABP_Zombie_C::StaticClass();
    const ClassificationRoots Roots{
        AKPickup::StaticClass(),
        ABP_BasePickupFirearm_C::StaticClass(),
        ABP_BasePickupMelee_C::StaticClass(),
        AKPickup_DeployableItem::StaticClass(),
        AKItemRack::StaticClass(),
        AKContainer::StaticClass(),
        ABP_Container_1D_Base_C::StaticClass(),
        AKWorldObjective::StaticClass(),
        AKExtractionPoint::StaticClass(),
        AKExtractionVehicle::StaticClass(),
        ABPA_VehicleParent_C::StaticClass()
    };
    if (!IsUsableObject(PlayerClass) || !IsUsableObject(ZombieClass) || !Roots.IsValid() ||
        !IsUsableObject(Roots.Pickup) || !IsUsableObject(Roots.FirearmPickup) ||
        !IsUsableObject(Roots.MeleePickup) || !IsUsableObject(Roots.DeployablePickup) ||
        !IsUsableObject(Roots.ItemRack) || !IsUsableObject(Roots.Container) ||
        !IsUsableObject(Roots.BlueprintContainer) || !IsUsableObject(Roots.WorldObjective) ||
        !IsUsableObject(Roots.ExtractionPoint) || !IsUsableObject(Roots.ExtractionVehicle) ||
        !IsUsableObject(Roots.Vehicle))
        return;

    int32 VisiblePlayers = 0;
    int32 VisibleZombies = 0;
    int32 ScannedActors = 0;
    std::array<int32, static_cast<std::size_t>(MarkerCategory::Count)> CategoryCounts{};
    std::vector<MarkerCandidate> Markers;
    Markers.reserve(512);
    std::vector<AKCharacter*> LivingZombies;
    LivingZombies.reserve(512);
    AimTargetCandidate CrosshairTarget{};
    AimTargetCandidate AimTarget{};
    AActor* NearestObjective{};
    AActor* NearestExtraction{};
    AActor* CrosshairPickup{};
    double NearestObjectiveDistance = std::numeric_limits<double>::max();
    double NearestExtractionDistance = std::numeric_limits<double>::max();
    double CrosshairPickupDistanceSquared = std::numeric_limits<double>::max();
    std::vector<AActor*> VacuumPickups;
    VacuumPickups.reserve(256);
    std::vector<RadarPoint> RadarPoints;
    RadarPoints.reserve(1024);
    std::vector<ThreatCandidate> Threats;
    Threats.reserve(256);
    std::vector<ObjectiveCandidate> Objectives;
    Objectives.reserve(64);
    std::vector<InspectorActorEntry> InspectorEntries;
    if (g_Settings.EnableActorInspector)
        InspectorEntries.reserve(static_cast<std::size_t>(std::clamp(g_Settings.InspectorMaxEntries, 100, 2500)));

    const auto ProcessActor = [&](AActor* Actor)
    {
        const ScopedDiagnosticStage ActorDiagnosticScope(DiagnosticStage::ActorValidation);
        g_DiagnosticObject = Actor;
        g_DiagnosticClass = nullptr;
        if (Actor)
            g_DiagnosticClass = Actor->Class;

        if (!IsUsableObject(Actor) || !IsUsableObject(Actor->Class) || Actor == LocalPawn)
            return;

        const bool IsPlayer = Actor->IsA(PlayerClass);
        const bool IsZombie = !IsPlayer && Actor->IsA(ZombieClass);

        if (IsPlayer || IsZombie)
        {
            g_DiagnosticStage = DiagnosticStage::CharacterRender;
            auto* Character = static_cast<AKCharacter*>(Actor);
            if (!Character->IsAlive())
                return;

            if (IsZombie && LivingZombies.size() < 4096)
                LivingZombies.push_back(Character);
            if (IsZombie && g_Settings.ModifyZombieTime)
                ApplyZombieTimeScale(Character, g_Settings.ZombieTimeScale);

            FVector BoundsOrigin{};
            FVector BoundsExtent{};
            Actor->GetActorBounds(false, &BoundsOrigin, &BoundsExtent, true);

            const double Distance = LocalPawn ? DistanceMeters(LocalLocation, BoundsOrigin) : 0.0;
            if (g_Settings.EnableActorInspector &&
                InspectorEntries.size() < static_cast<std::size_t>(std::clamp(g_Settings.InspectorMaxEntries, 100, 2500)))
            {
                InspectorEntries.push_back(InspectorActorEntry{
                    reinterpret_cast<std::uintptr_t>(Actor), Actor->Class->GetName(),
                    IsPlayer ? L"Player" : ZombieTypeLabel(Character), MarkerCategory::None,
                    Distance, BoundsOrigin, true});
            }
            if (IsZombie)
            {
                if (g_Settings.ShowRadar && g_Settings.RadarShowZombies &&
                    Distance <= static_cast<double>(g_Settings.RadarRangeMeters))
                {
                    RadarPoints.push_back(RadarPoint{BoundsOrigin, MarkerCategory::None, true, Distance});
                }
                if (g_Settings.ShowThreatIndicators &&
                    Distance <= static_cast<double>(g_Settings.ThreatRadiusMeters))
                {
                    Threats.push_back(ThreatCandidate{BoundsOrigin, Distance});
                }
            }
            if (LocalPawn && Distance > g_Settings.RadiusMeters)
                return;

            const double HalfHeight = std::clamp(std::abs(BoundsExtent.Z), 25.0, 350.0);
            const FVector TopWorld{BoundsOrigin.X, BoundsOrigin.Y, BoundsOrigin.Z + HalfHeight};
            const FVector BottomWorld{BoundsOrigin.X, BoundsOrigin.Y, BoundsOrigin.Z - HalfHeight};
            const FVector AimWorld{
                BoundsOrigin.X,
                BoundsOrigin.Y,
                BoundsOrigin.Z + (HalfHeight * 0.78)
            };

            FVector2D TopScreen{};
            FVector2D BottomScreen{};
            if (!Controller->ProjectWorldLocationToScreen(TopWorld, &TopScreen, false) ||
                !Controller->ProjectWorldLocationToScreen(BottomWorld, &BottomScreen, false))
            {
                return;
            }

            FVector2D AimScreen = TopScreen;
            Controller->ProjectWorldLocationToScreen(AimWorld, &AimScreen, false);

            if (IsZombie && HasScreenCenter &&
                std::isfinite(AimScreen.X) && std::isfinite(AimScreen.Y))
            {
                const double DeltaX = AimScreen.X - ScreenCenter.X;
                const double DeltaY = AimScreen.Y - ScreenCenter.Y;
                const double ScreenDistanceSquared = (DeltaX * DeltaX) + (DeltaY * DeltaY);
                if (ScreenDistanceSquared < CrosshairTarget.ScreenDistanceSquared)
                {
                    CrosshairTarget = AimTargetCandidate{
                        Character,
                        AimWorld,
                        AimScreen,
                        ScreenDistanceSquared,
                        Distance,
                        false
                    };
                }

                const double FovSquared =
                    static_cast<double>(g_Settings.AimFovPixels) *
                    static_cast<double>(g_Settings.AimFovPixels);
                if (g_Settings.EnableZombieAimbot && ScreenDistanceSquared <= FovSquared &&
                    ScreenDistanceSquared < AimTarget.ScreenDistanceSquared)
                {
                    const bool Visible = !g_Settings.AimVisibilityCheck ||
                        Controller->LineOfSightTo(Character, ViewLocation, false);
                    if (Visible)
                    {
                        AimTarget = AimTargetCandidate{
                            Character,
                            AimWorld,
                            AimScreen,
                            ScreenDistanceSquared,
                            Distance,
                            true
                        };
                    }
                }
            }

            if ((IsPlayer && !g_Settings.ShowPlayers) || (IsZombie && !g_Settings.ShowZombies))
                return;

            const double Height = std::abs(BottomScreen.Y - TopScreen.Y);
            if (!std::isfinite(Height) || Height < 4.0 || Height > 5000.0)
                return;

            const double Width = Height * 0.45;
            const double Left = TopScreen.X - (Width * 0.5);
            const double Top = std::min(TopScreen.Y, BottomScreen.Y);

            const FLinearColor Color = IsPlayer
                ? ArrayColor(g_Settings.PlayerEspColor)
                : ArrayColor(g_Settings.ZombieEspColor);

            DrawBox(
                Hud,
                static_cast<float>(Left),
                static_cast<float>(Top),
                static_cast<float>(Width),
                static_cast<float>(Height),
                Color,
                g_Settings.BoxThickness);

            if (g_Settings.ShowHealthBars)
            {
                DrawHealthBar(
                    Hud,
                    Character,
                    static_cast<float>(Left),
                    static_cast<float>(Top),
                    static_cast<float>(Height));
            }

            if (g_Settings.ShowSkeleton)
                DrawCharacterSkeleton(Hud, Controller, Character);

            if (g_Settings.ShowHeadMarkers)
            {
                FVector2D HeadScreen = AimScreen;
                if (g_Settings.ShowSkeleton && IsUsableObject(Character->Mesh))
                {
                    const SkeletonBoneSet& Bones = GetSkeletonBones(Character->Mesh);
                    ProjectBone(Character->Mesh, Bones, 0, Controller, &HeadScreen);
                }
                const float HeadX = static_cast<float>(HeadScreen.X);
                const float HeadY = static_cast<float>(HeadScreen.Y);
                const float HeadSize = 5.0F;
                const FLinearColor HeadColor = ArrayColor(g_Settings.HeadMarkerColor);
                DrawBox(Hud, HeadX - HeadSize, HeadY - HeadSize, HeadSize * 2.0F, HeadSize * 2.0F, HeadColor, 1.5F);
                Hud->DrawLine(HeadX - HeadSize - 3.0F, HeadY, HeadX + HeadSize + 3.0F, HeadY, HeadColor, 1.0F);
            }

            if (g_Settings.ShowSnaplines && HasScreenCenter)
            {
                Hud->DrawLine(
                    static_cast<float>(ScreenCenter.X), static_cast<float>(ScreenCenter.Y * 2.0 - 18.0),
                    static_cast<float>(BottomScreen.X), static_cast<float>(BottomScreen.Y),
                    ArrayColor(g_Settings.SnaplineColor), 1.0F);
            }

            DrawText(
                Hud,
                MakeEntityLabel(Character, IsPlayer, Distance),
                static_cast<float>(Left),
                static_cast<float>(Top - 18.0),
                Color,
                g_Settings.TextScale);

            if (IsPlayer)
                ++VisiblePlayers;
            else
                ++VisibleZombies;

            return;
        }

        g_DiagnosticStage = DiagnosticStage::ClassifyActor;
        const ClassDescriptor& Descriptor = DescribeActorClass(Actor, Roots);
        if (Descriptor.Category == MarkerCategory::None)
            return;

        g_DiagnosticStage = DiagnosticStage::ActorLocation;
        const FVector Location = Actor->K2_GetActorLocation();
        const double Distance = LocalPawn ? DistanceMeters(LocalLocation, Location) : 0.0;

        if (g_Settings.EnableActorInspector &&
            (g_Settings.InspectorIncludeAllActors || Descriptor.Category != MarkerCategory::DebugActor) &&
            InspectorEntries.size() < static_cast<std::size_t>(std::clamp(g_Settings.InspectorMaxEntries, 100, 2500)))
        {
            InspectorEntries.push_back(InspectorActorEntry{
                reinterpret_cast<std::uintptr_t>(Actor), Descriptor.RawName, Descriptor.FriendlyName,
                Descriptor.Category, Distance, Location, false});
        }

        if (g_Settings.ShowRadar && Distance <= static_cast<double>(g_Settings.RadarRangeMeters))
        {
            if ((g_Settings.RadarShowLoot && IsLootCategory(Descriptor.Category)) ||
                (g_Settings.RadarShowObjectives &&
                 (Descriptor.Category == MarkerCategory::Objective || Descriptor.Category == MarkerCategory::Extraction)))
            {
                RadarPoints.push_back(RadarPoint{Location, Descriptor.Category, false, Distance});
            }
        }

        if (Descriptor.Category == MarkerCategory::Objective && Actor->IsA(Roots.WorldObjective))
        {
            auto* Objective = static_cast<AKWorldObjective*>(Actor);
            Objectives.push_back(ObjectiveCandidate{
                Location, Descriptor.FriendlyName, Distance, Objective->IsActive(), Objective->IsComplete()});
        }

        if (Descriptor.Category == MarkerCategory::Objective && Distance < NearestObjectiveDistance)
        {
            NearestObjective = Actor;
            NearestObjectiveDistance = Distance;
        }
        else if (Descriptor.Category == MarkerCategory::Extraction && Distance < NearestExtractionDistance)
        {
            NearestExtraction = Actor;
            NearestExtractionDistance = Distance;
        }

        if (LocalPawn && IsLootCategory(Descriptor.Category) && Actor->IsA(Roots.Pickup))
        {
            if (Distance <= std::clamp(
                    g_Settings.PickupVacuumRadiusMeters,
                    kMinRadiusMeters,
                    kMaxRadiusMeters) && VacuumPickups.size() < 4096)
            {
                VacuumPickups.push_back(Actor);
            }
            if (HasScreenCenter)
            {
                FVector2D PickupScreen{};
                if (Controller->ProjectWorldLocationToScreen(Location, &PickupScreen, false))
                {
                    const double DeltaX = PickupScreen.X - ScreenCenter.X;
                    const double DeltaY = PickupScreen.Y - ScreenCenter.Y;
                    const double ScreenDistanceSquared = (DeltaX * DeltaX) + (DeltaY * DeltaY);
                    if (ScreenDistanceSquared < CrosshairPickupDistanceSquared)
                    {
                        CrosshairPickup = Actor;
                        CrosshairPickupDistanceSquared = ScreenDistanceSquared;
                    }
                }
            }
        }

        if (!IsCategoryEnabled(Descriptor.Category, Descriptor.Ammo))
            return;
        if (LocalPawn && Distance > g_Settings.RadiusMeters)
            return;

        const std::size_t CategoryIndex = static_cast<std::size_t>(Descriptor.Category);
        if (CategoryIndex < CategoryCounts.size())
            ++CategoryCounts[CategoryIndex];

        Markers.push_back(MarkerCandidate{Location, Distance, Descriptor, Actor});
    };

    const auto ProcessLevel = [&](ULevel* Level, int32 LevelIndex)
    {
        g_DiagnosticStage = DiagnosticStage::ActorScan;
        g_DiagnosticLevelIndex = LevelIndex;
        g_DiagnosticActorIndex = -1;
        g_DiagnosticObject = Level;
        g_DiagnosticClass = Level ? Level->Class : nullptr;
        if (!IsUsableObject(Level))
            return;

        const auto& Actors = Level->Actors;
        const int32 ActorCount = Actors.Num();
        if (ActorCount <= 0 || ActorCount > 100000)
            return;

        for (int32 Index = 0; Index < ActorCount && ScannedActors < 200000; ++Index)
        {
            g_DiagnosticStage = DiagnosticStage::ActorScan;
            g_DiagnosticActorIndex = Index;
            g_DiagnosticObject = nullptr;
            g_DiagnosticClass = nullptr;
            ++ScannedActors;
            ProcessActor(Actors[Index]);
        }
    };

    const int32 LevelCount = World->Levels.Num();
    if (LevelCount > 0 && LevelCount <= 256)
    {
        for (int32 LevelIndex = 0; LevelIndex < LevelCount && ScannedActors < 200000; ++LevelIndex)
            ProcessLevel(World->Levels[LevelIndex], LevelIndex);
    }
    else
    {
        ProcessLevel(World->PersistentLevel, -1);
    }

    g_DiagnosticStage = DiagnosticStage::ActorScan;
    g_DiagnosticLevelIndex = -1;
    g_DiagnosticActorIndex = -1;
    g_DiagnosticObject = World;
    g_DiagnosticClass = World->Class;

    {
        std::scoped_lock InspectorLock(g_InspectorMutex);
        if (g_Settings.EnableActorInspector)
            g_InspectorSnapshot = InspectorEntries;
        else
            g_InspectorSnapshot.clear();
    }

    const bool HasAimTarget = IsUsableObject(AimTarget.Character);
    g_AimTargetActive.store(HasAimTarget, std::memory_order_release);
    if (HasScreenCenter && g_Settings.EnableZombieAimbot && g_Settings.DrawAimFov)
        DrawAimFovCircle(Hud, ScreenCenter, g_Settings.AimFovPixels, HasAimTarget);
    if (HasAimTarget)
    {
        constexpr float TargetMarker = 7.0F;
        const float X = static_cast<float>(AimTarget.ScreenLocation.X);
        const float Y = static_cast<float>(AimTarget.ScreenLocation.Y);
        Hud->DrawLine(X - TargetMarker, Y, X + TargetMarker, Y, MakeColor(0.25F, 1.0F, 0.45F), 2.0F);
        Hud->DrawLine(X, Y - TargetMarker, X, Y + TargetMarker, MakeColor(0.25F, 1.0F, 0.45F), 2.0F);
        ApplyZombieAim(Controller, AimTarget);
    }
    ExecuteTeleKillRequests(World, LivingZombies, CrosshairTarget);
    ExecuteWorldActionRequests(
        World,
        Controller,
        LocalPawn,
        NearestObjective,
        NearestExtraction,
        CrosshairPickup,
        VacuumPickups);

    std::stable_sort(
        Markers.begin(),
        Markers.end(),
        [](const MarkerCandidate& Left, const MarkerCandidate& Right)
        {
            return Left.Distance < Right.Distance;
        });

    const std::size_t MarkerLimit = std::min(
        Markers.size(),
        static_cast<std::size_t>(std::max(0, g_Settings.MaxMarkers)));
    for (std::size_t Index = 0; Index < MarkerLimit; ++Index)
    {
        const MarkerCandidate& Marker = Markers[Index];
        DrawPickupMarker(
            Hud,
            Controller,
            Marker.Location,
            MakeMarkerLabel(Marker.Descriptor, Marker.Distance),
            CategoryColor(Marker.Descriptor.Category));
    }

    std::stable_sort(Threats.begin(), Threats.end(),
        [](const ThreatCandidate& Left, const ThreatCandidate& Right) { return Left.Distance < Right.Distance; });
    if (HasScreenCenter && LocalPawn)
    {
        DrawRadar(Hud, ScreenCenter, LocalLocation, ViewRotation.Yaw, RadarPoints);
        DrawThreatIndicators(Hud, ScreenCenter, LocalLocation, ViewRotation.Yaw, Threats);
        DrawWeaponInformation(Hud, ScreenCenter, LocalPawn);
        DrawDiagnosticOverlay(Hud, ScreenCenter);
    }
    DrawObjectiveTracker(Hud, Objectives);

    int32 LootCount = 0;
    int32 WorldCount = 0;
    for (int Category = static_cast<int>(MarkerCategory::Firearm);
         Category < static_cast<int>(MarkerCategory::Count);
         ++Category)
    {
        const auto Value = static_cast<MarkerCategory>(Category);
        const int32 Count = CategoryCounts[static_cast<std::size_t>(Category)];
        if (IsLootCategory(Value))
            LootCount += Count;
        else if (IsWorldCategory(Value))
            WorldCount += Count;
    }

    const std::uint64_t FrameNumber = g_OverlayFrameCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    if (FrameNumber <= 3 || (FrameNumber % 300) == 0)
    {
        WriteDiagnosticLog(
            "Actor frame=%llu scanned=%d players=%d zombies=%d markers=%llu/%llu "
            "radar=%d threats=%d objectives=%d inspector=%d skeleton=%d weapon_hud=%d photo=%d",
            static_cast<unsigned long long>(FrameNumber),
            ScannedActors,
            VisiblePlayers,
            VisibleZombies,
            static_cast<unsigned long long>(MarkerLimit),
            static_cast<unsigned long long>(Markers.size()),
            g_Settings.ShowRadar ? 1 : 0,
            g_Settings.ShowThreatIndicators ? 1 : 0,
            g_Settings.ShowObjectiveTracker ? 1 : 0,
            g_Settings.EnableActorInspector ? 1 : 0,
            g_Settings.ShowSkeleton ? 1 : 0,
            g_Settings.ShowWeaponInfo ? 1 : 0,
            g_Settings.PhotoMode ? 1 : 0);
    }

    if (g_Settings.ShowStatus)
    {
        std::wostringstream Status;
        Status << L"Offline inspector  |  Players " << VisiblePlayers
               << L"  Zombies " << VisibleZombies
               << L"  Loot " << LootCount
               << L"  World " << WorldCount
               << L"  Markers " << MarkerLimit << L"/" << Markers.size()
               << L"  Radius " << std::fixed << std::setprecision(0)
               << g_Settings.RadiusMeters << L"m";
        if (g_Settings.EnableOfflineDamageModifier || g_Settings.OneHitMelee)
        {
            Status << L"  Damage ";
            if (g_DamageModifierStatus.load(std::memory_order_relaxed) ==
                DamageModifierStatus::Applied)
            {
                if (g_Settings.OneHitMelee)
                    Status << L"MELEE 1-HIT";
                else
                    Status << std::fixed << std::setprecision(2)
                           << g_Settings.OfflineDamageMultiplier << L"x";
            }
            else
            {
                Status << L"waiting";
            }
        }
        if (g_Settings.EnableZombieAimbot)
            Status << L"  Aim " << (HasAimTarget ? L"LOCK" : L"SCAN");
        if (g_Settings.GhostMode)
            Status << L"  Ghost";
        if (g_Settings.SpeedHack)
            Status << L"  Speed " << std::fixed << std::setprecision(2)
                   << g_Settings.SpeedMultiplier << L"x";
        Status << (g_ImGuiFailed.load(std::memory_order_relaxed) ? L"  |  UI fallback" : L"")
               << L"  |  Hotkeys on CONFIG";
        DrawText(Hud, Status.str(), 20.0F, 20.0F, MakeColor(0.85F, 0.95F, 1.0F), 0.85F);
    }

    RenderLegend(Hud);
    if (!g_ImGuiReady.load(std::memory_order_acquire))
        RenderSettingsMenu(Hud);
}

void __fastcall HookedProcessEvent(const UObject* Object, UFunction* Function, void* Parameters)
{
    const ActiveHookCall ActiveCall{};
    const ProcessEventFn Original = g_OriginalProcessEvent.load(std::memory_order_acquire);
    if (!Original)
        return;

    Original(Object, Function, Parameters);

    static thread_local bool IsDrawing = false;
    if (IsDrawing || !Object || !Function)
        return;

    if (Function->GetName() != "ReceiveDrawHUD")
        return;

    if (!Object->IsA(AHUD::StaticClass()))
        return;

    int OverlayKey{};
    int TeleKillSingleKey{};
    int TeleKillAllKey{};
    {
        std::scoped_lock SettingsLock(g_SettingsMutex);
        OverlayKey = g_Settings.OverlayHotkey;
        TeleKillSingleKey = g_Settings.TeleKillSingleHotkey;
        TeleKillAllKey = g_Settings.TeleKillAllHotkey;
    }
    if ((GetAsyncKeyState(OverlayKey) & 1) != 0)
        g_OverlayEnabled.store(!g_OverlayEnabled.load(std::memory_order_relaxed), std::memory_order_relaxed);
    if ((GetAsyncKeyState(TeleKillSingleKey) & 1) != 0)
        g_RequestTeleKillSingle.store(true, std::memory_order_release);
    if ((GetAsyncKeyState(TeleKillAllKey) & 1) != 0)
        g_RequestTeleKillAll.store(true, std::memory_order_release);

    // Gameplay modifiers are maintained even when visual ESP is hidden. Every
    // modifier independently fails closed and restores when a NetDriver appears.
    IsDrawing = true;
    TickOfflineGameplayModifiers();
    IsDrawing = false;

    if (!g_OverlayEnabled.load(std::memory_order_relaxed))
    {
        g_AimTargetActive.store(false, std::memory_order_release);
        return;
    }

    if (!g_ImGuiReady.load(std::memory_order_acquire))
        HandleMenuInput();

    IsDrawing = true;
    RenderOfflineOverlay(static_cast<AHUD*>(const_cast<UObject*>(Object)));
    IsDrawing = false;
}

bool PatchHudProcessEvent(AHUD* Hud)
{
    const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::PatchHud);
    if (!Hud)
        return false;

    auto** VTable = *reinterpret_cast<void***>(Hud);
    if (!VTable)
        return false;

    void** Slot = &VTable[Offsets::ProcessEventIdx];
    const void* HookAddress = reinterpret_cast<void*>(&HookedProcessEvent);

    if (*Slot == HookAddress)
        return true;

    for (const VTablePatch& Patch : g_Patches)
    {
        if (Patch.Slot == Slot)
            return true;
    }

    auto Original = reinterpret_cast<ProcessEventFn>(*Slot);
    if (!Original)
        return false;

    const ProcessEventFn ExistingOriginal = g_OriginalProcessEvent.load(std::memory_order_acquire);
    if (ExistingOriginal && Original != ExistingOriginal)
        return false;

    DWORD OldProtection{};
    if (!VirtualProtect(Slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &OldProtection))
        return false;

    if (!ExistingOriginal)
        g_OriginalProcessEvent.store(Original, std::memory_order_release);

    InterlockedExchangePointer(
        reinterpret_cast<PVOID volatile*>(Slot),
        reinterpret_cast<PVOID>(&HookedProcessEvent));

    DWORD Ignored{};
    VirtualProtect(Slot, sizeof(void*), OldProtection, &Ignored);
    FlushInstructionCache(GetCurrentProcess(), Slot, sizeof(void*));

    g_Patches.push_back(VTablePatch{Slot, reinterpret_cast<void*>(Original)});
    if (!g_HudPatchLogged.exchange(true, std::memory_order_acq_rel))
    {
        WriteDiagnosticLog(
            "HUD ProcessEvent patched: hud=%p slot=%p original=%p",
            Hud,
            Slot,
            reinterpret_cast<void*>(Original));
    }
    return true;
}

void RestoreHooks()
{
    for (auto It = g_Patches.rbegin(); It != g_Patches.rend(); ++It)
    {
        if (!It->Slot || !It->Original)
            continue;

        DWORD OldProtection{};
        if (!VirtualProtect(It->Slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &OldProtection))
            continue;

        if (*It->Slot == reinterpret_cast<void*>(&HookedProcessEvent))
        {
            InterlockedExchangePointer(
                reinterpret_cast<PVOID volatile*>(It->Slot),
                It->Original);
        }

        DWORD Ignored{};
        VirtualProtect(It->Slot, sizeof(void*), OldProtection, &Ignored);
        FlushInstructionCache(GetCurrentProcess(), It->Slot, sizeof(void*));
    }

    g_Patches.clear();
}

DWORD WINAPI OverlayWorker(void* ModuleAddress)
{
    const HMODULE Module = static_cast<HMODULE>(ModuleAddress);
    {
        const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::WorkerStart);
        OpenDiagnosticLog(Module);
        WriteDiagnosticLog("Diagnostic build started; safe-start actor delay is 10000 ms");
        g_ExceptionHandler = AddVectoredExceptionHandler(1, &OverlayExceptionHandler);
        WriteDiagnosticLog(
            "Vectored exception logger %s",
            g_ExceptionHandler ? "installed" : "could not be installed");
    }

    bool ExternalUiStarted = false;
    {
        const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::ExternalUiStart);
        ExternalUiStarted = StartExternalImGuiWindow();
        WriteDiagnosticLog(
            "Private D3D11 ImGui thread %s; internal game DX12 hooks are disabled",
            ExternalUiStarted ? "started" : "unavailable; Unreal HUD fallback remains enabled");
    }

    while (g_Running.load(std::memory_order_relaxed))
    {
        int UnloadKey{};
        {
            std::scoped_lock SettingsLock(g_SettingsMutex);
            UnloadKey = g_Settings.UnloadHotkey;
        }
        if ((GetAsyncKeyState(UnloadKey) & 1) != 0)
        {
            g_UnloadRequested.store(true, std::memory_order_release);
            WriteDiagnosticLog("Unload requested; waiting for game-thread modifier restoration");
        }

        {
            const ScopedDiagnosticStage DiagnosticScope(DiagnosticStage::PatchHud);
            UWorld* World = UWorld::GetWorld();
            g_OfflineAllowed.store(IsOfflineWorld(World), std::memory_order_relaxed);
            if (APlayerController* Controller = GetLocalController(World))
                PatchHudProcessEvent(Controller->MyHUD);
        }

        if (g_UnloadRequested.load(std::memory_order_acquire) &&
            !AnyGameplayModifierApplied())
        {
            g_Running.store(false, std::memory_order_relaxed);
            break;
        }

        Sleep(kHookPollMilliseconds);
    }

    g_DiagnosticStage = DiagnosticStage::Unload;
    WriteDiagnosticLog("Modifier restoration complete; disabling HUD hook");
    for (int Attempt = 0;
         Attempt < 300 && AnyGameplayModifierApplied();
         ++Attempt)
    {
        Sleep(10);
    }
    if (AnyGameplayModifierApplied())
    {
        WriteDiagnosticLog("HUD restoration timed out; using guarded direct-value fallback");
        RestoreOfflineDamageModifier(false);
        RestoreMeleeRangeModifier(false);
        RestoreSpeedModifier();
        RestoreWeaponModifier(false);
        RestoreStaminaModifier(false);
        RestoreGodMode();
        RestoreZombieTime();
    }
    if (g_FullbrightApplied.load(std::memory_order_acquire) ||
        g_GhostModeApplied.load(std::memory_order_acquire) ||
        g_GlobalTimeApplied.load(std::memory_order_acquire) ||
        g_AmmoCheatApplied.load(std::memory_order_acquire) ||
        g_PhotoModeApplied.load(std::memory_order_acquire))
    {
        WriteDiagnosticLog("Unload stopped safely: a game-thread-only restoration did not complete");
        return 0;
    }
    RestoreHooks();
    if (ExternalUiStarted && !StopExternalImGuiWindow())
    {
        WriteDiagnosticLog("Unload stopped safely: external ImGui thread did not exit");
        return 0;
    }

    // Once every hook entry point is disabled, give any frame already inside
    // a hook time to leave its trampoline before those trampolines are freed.
    for (int Attempt = 0; Attempt < 1000 && g_ActiveHookCalls.load(std::memory_order_acquire) != 0; ++Attempt)
        Sleep(10);

    if (g_ActiveHookCalls.load(std::memory_order_acquire) != 0)
    {
        WriteDiagnosticLog(
            "Unload stopped safely: %lu hook calls did not drain",
            static_cast<unsigned long>(g_ActiveHookCalls.load(std::memory_order_relaxed)));
        if (g_ExceptionHandler)
        {
            RemoveVectoredExceptionHandler(g_ExceptionHandler);
            g_ExceptionHandler = nullptr;
        }
        if (g_LogHandle && g_LogHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_LogHandle);
            g_LogHandle = nullptr;
        }
        return 0;
    }

    g_OriginalProcessEvent.store(nullptr, std::memory_order_release);
    WriteDiagnosticLog("Unload complete");
    if (g_ExceptionHandler)
    {
        RemoveVectoredExceptionHandler(g_ExceptionHandler);
        g_ExceptionHandler = nullptr;
    }
    if (g_LogHandle && g_LogHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_LogHandle);
        g_LogHandle = nullptr;
    }
    FreeLibraryAndExitThread(Module, 0);
    return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE Module, DWORD Reason, void*)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Module);

        if (HANDLE Thread = CreateThread(nullptr, 0, OverlayWorker, Module, 0, nullptr))
            CloseHandle(Thread);
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        g_Running.store(false, std::memory_order_relaxed);
    }

    return TRUE;
}
