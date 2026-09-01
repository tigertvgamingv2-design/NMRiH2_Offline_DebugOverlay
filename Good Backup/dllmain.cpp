#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
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

namespace
{
using namespace SDK;

using ProcessEventFn = void(__fastcall*)(const UObject*, UFunction*, void*);

constexpr double kMinRadiusMeters = 25.0;
constexpr double kMaxRadiusMeters = 5000.0;
constexpr double kRadiusStepMeters = 25.0;
constexpr DWORD kHookPollMilliseconds = 500;

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
    Loot,
    Ammo,
    World,
    Count
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
    float MarkerSize{6.0F};
    float TextScale{0.9F};
    float BoxThickness{1.5F};
    int MaxMarkers{150};
    bool MenuOpen{true};
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

std::atomic_bool g_Running{true};
std::atomic_bool g_OverlayEnabled{true};
std::atomic_uint32_t g_ActiveHookCalls{0};
std::atomic<ProcessEventFn> g_OriginalProcessEvent{nullptr};
std::vector<VTablePatch> g_Patches;
OverlaySettings g_Settings;
std::unordered_map<const UClass*, ClassDescriptor> g_ClassCache;

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

FLinearColor MakeColor(float R, float G, float B, float A = 1.0F)
{
    return FLinearColor{R, G, B, A};
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

    Hud->DrawRect(MakeColor(0.025F, 0.035F, 0.055F, 0.88F), Left, Top, Width, Height);
    DrawBox(Hud, Left, Top, Width, Height, MakeColor(0.25F, 0.70F, 1.0F));
}

void DrawHealthBar(AHUD* Hud, AKCharacter* Character, float Left, float Top, float Height)
{
    if (!Hud || !Character || !Character->HealthComponent || Height <= 0.0F)
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
    switch (Category)
    {
    case MarkerCategory::Firearm: return MakeColor(0.25F, 1.0F, 0.35F);
    case MarkerCategory::Melee: return MakeColor(1.0F, 0.52F, 0.18F);
    case MarkerCategory::Ammo: return MakeColor(1.0F, 0.82F, 0.18F);
    case MarkerCategory::Medical: return MakeColor(0.15F, 1.0F, 0.82F);
    case MarkerCategory::Explosive: return MakeColor(1.0F, 0.18F, 0.12F);
    case MarkerCategory::Attachment: return MakeColor(0.82F, 0.42F, 1.0F);
    case MarkerCategory::Equipment: return MakeColor(0.30F, 0.62F, 1.0F);
    case MarkerCategory::Supply: return MakeColor(0.45F, 1.0F, 0.62F);
    case MarkerCategory::Deployable: return MakeColor(0.12F, 0.85F, 0.88F);
    case MarkerCategory::ObjectiveItem: return MakeColor(1.0F, 0.35F, 0.82F);
    case MarkerCategory::MiscPickup: return MakeColor(0.80F, 0.84F, 0.90F);
    case MarkerCategory::Container: return MakeColor(0.35F, 0.82F, 1.0F);
    case MarkerCategory::Objective: return MakeColor(1.0F, 0.25F, 0.72F);
    case MarkerCategory::Extraction: return MakeColor(0.25F, 1.0F, 0.25F);
    case MarkerCategory::Interactable: return MakeColor(0.95F, 0.95F, 1.0F);
    case MarkerCategory::Door: return MakeColor(0.76F, 0.52F, 0.30F);
    case MarkerCategory::Vehicle: return MakeColor(0.30F, 0.72F, 1.0F);
    case MarkerCategory::DebugActor: return MakeColor(0.62F, 0.65F, 0.70F);
    default: return MakeColor(0.70F, 0.70F, 0.70F);
    }
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
    if (!Actor || !Actor->Class)
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
    case MenuTab::Loot: return L"LOOT";
    case MenuTab::Ammo: return L"AMMO";
    case MenuTab::World: return L"WORLD";
    default: return L"UNKNOWN";
    }
}

int MenuRowCount(MenuTab Tab)
{
    switch (Tab)
    {
    case MenuTab::General: return 15;
    case MenuTab::Loot: return 12;
    case MenuTab::Ammo: return 9;
    case MenuTab::World: return 8;
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
                g_Settings.MenuOpen = true;
                g_Settings.ActiveTab = CurrentTab;
                g_Settings.SelectedMenuRow = 14;
            }
            return;
        default:
            break;
        }
    }

    if (bool* Value = ToggleForMenuRow(Tab, Row))
        *Value = !*Value;
}

void HandleMenuInput()
{
    if ((GetAsyncKeyState(VK_F1) & 1) != 0)
        g_Settings.MenuOpen = !g_Settings.MenuOpen;

    if (!g_Settings.MenuOpen)
        return;

    if ((GetAsyncKeyState(VK_TAB) & 1) != 0)
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
    else if (Tab == MenuTab::Ammo)
    {
        const wchar_t* Names[]{L"All ammunition", L"9mm", L".45 ACP", L".22 LR", L".357", L".308", L"5.56mm", L"6.8x51mm", L"12 gauge"};
        if (Row >= 0 && Row < 9)
            Text << Names[Row] << L": " << ToggleLabel(*ToggleForMenuRow(Tab, Row));
    }
    else if (Tab == MenuTab::World)
    {
        const wchar_t* Names[]{L"All world markers", L"Loot containers / racks", L"Objectives", L"Extraction points", L"Powered interactables", L"Doors", L"Vehicles", L"Other SDK actors (debug)"};
        if (Row >= 0 && Row < 8)
            Text << Names[Row] << L": " << ToggleLabel(*ToggleForMenuRow(Tab, Row));
    }
    return Text.str();
}

void RenderSettingsMenu(AHUD* Hud)
{
    if (!Hud || !g_Settings.MenuOpen)
        return;

    constexpr float Left = 20.0F;
    constexpr float Top = 92.0F;
    constexpr float Width = 470.0F;
    constexpr float RowHeight = 18.0F;
    const int RowCount = MenuRowCount(g_Settings.ActiveTab);
    const float Height = 88.0F + (static_cast<float>(RowCount) * RowHeight);

    DrawPanel(Hud, Left, Top, Width, Height);
    DrawText(Hud, L"NMRiH2 OFFLINE INSPECTOR", Left + 12.0F, Top + 9.0F, MakeColor(0.35F, 0.80F, 1.0F));
    DrawText(
        Hud,
        std::wstring(L"TAB: ") + MenuTabLabel(g_Settings.ActiveTab) + L"  (press Tab for next page)",
        Left + 12.0F,
        Top + 29.0F,
        MakeColor(0.70F, 0.82F, 1.0F),
        0.9F);

    for (int Row = 0; Row < RowCount; ++Row)
    {
        const bool Selected = Row == g_Settings.SelectedMenuRow;
        const std::wstring Prefix = Selected ? L"> " : L"  ";
        const FLinearColor Color = Selected
            ? MakeColor(1.0F, 0.85F, 0.25F)
            : MakeColor(0.88F, 0.92F, 1.0F);
        DrawText(Hud, Prefix + MenuRowText(g_Settings.ActiveTab, Row), Left + 12.0F, Top + 51.0F + (Row * RowHeight), Color, 0.9F);
    }

    DrawText(
        Hud,
        L"F1 close | Tab page | Up/Down select | Left/Right change | Enter",
        Left + 12.0F,
        Top + Height - 23.0F,
        MakeColor(0.62F, 0.70F, 0.80F),
        0.8F);
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
    if (!Pawn || !Pawn->PlayerState)
        return L"Player";

    const std::string Name = Pawn->PlayerState->GetPlayerName().ToString();
    const std::wstring WideName = Utf8ToWide(Name);
    return WideName.empty() ? L"Player" : WideName;
}

std::wstring MakeEntityLabel(AKCharacter* Character, bool IsPlayer, double Distance)
{
    std::wostringstream Label;
    Label << (IsPlayer ? GetPlayerLabel(static_cast<APawn*>(Character)) : L"Zombie");

    if (g_Settings.ShowDistance)
        Label << L"  " << std::fixed << std::setprecision(0) << Distance << L"m";

    if (g_Settings.ShowHealth && Character && Character->HealthComponent)
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
    if (!World || !World->OwningGameInstance)
        return nullptr;

    auto& LocalPlayers = World->OwningGameInstance->LocalPlayers;
    if (LocalPlayers.Num() <= 0 || !LocalPlayers[0])
        return nullptr;

    return LocalPlayers[0]->PlayerController;
}

bool IsOfflineWorld(const UWorld* World)
{
    // Deliberately fail closed: entity information is never rendered while an
    // Unreal network driver is attached to the active world.
    return World && World->NetDriver == nullptr;
}

void RenderOfflineOverlay(AHUD* Hud)
{
    if (!Hud)
        return;

    UWorld* World = UWorld::GetWorld();
    if (!World)
        return;

    if (!IsOfflineWorld(World))
    {
        DrawText(
            Hud,
            L"NMRiH2 overlay locked: a network session was detected",
            20.0F,
            20.0F,
            MakeColor(1.0F, 0.25F, 0.25F));
        return;
    }

    APlayerController* Controller = GetLocalController(World);
    if (!Controller)
        return;

    APawn* LocalPawn = Controller->AcknowledgedPawn;
    const FVector LocalLocation = LocalPawn ? LocalPawn->K2_GetActorLocation() : FVector{};

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
    if (!PlayerClass || !ZombieClass || !Roots.IsValid())
        return;

    int32 VisiblePlayers = 0;
    int32 VisibleZombies = 0;
    int32 ScannedActors = 0;
    std::array<int32, static_cast<std::size_t>(MarkerCategory::Count)> CategoryCounts{};
    std::vector<MarkerCandidate> Markers;
    Markers.reserve(512);

    const auto ProcessActor = [&](AActor* Actor)
    {
        if (!Actor || Actor == LocalPawn)
            return;

        const bool IsPlayer = Actor->IsA(PlayerClass);
        const bool IsZombie = !IsPlayer && Actor->IsA(ZombieClass);

        if (IsPlayer || IsZombie)
        {
            if ((IsPlayer && !g_Settings.ShowPlayers) || (IsZombie && !g_Settings.ShowZombies))
                return;

            auto* Character = static_cast<AKCharacter*>(Actor);
            if (!Character->IsAlive())
                return;

            FVector BoundsOrigin{};
            FVector BoundsExtent{};
            Actor->GetActorBounds(false, &BoundsOrigin, &BoundsExtent, true);

            const double Distance = LocalPawn ? DistanceMeters(LocalLocation, BoundsOrigin) : 0.0;
            if (LocalPawn && Distance > g_Settings.RadiusMeters)
                return;

            const double HalfHeight = std::clamp(std::abs(BoundsExtent.Z), 25.0, 350.0);
            const FVector TopWorld{BoundsOrigin.X, BoundsOrigin.Y, BoundsOrigin.Z + HalfHeight};
            const FVector BottomWorld{BoundsOrigin.X, BoundsOrigin.Y, BoundsOrigin.Z - HalfHeight};

            FVector2D TopScreen{};
            FVector2D BottomScreen{};
            if (!Controller->ProjectWorldLocationToScreen(TopWorld, &TopScreen, false) ||
                !Controller->ProjectWorldLocationToScreen(BottomWorld, &BottomScreen, false))
            {
                return;
            }

            const double Height = std::abs(BottomScreen.Y - TopScreen.Y);
            if (!std::isfinite(Height) || Height < 4.0 || Height > 5000.0)
                return;

            const double Width = Height * 0.45;
            const double Left = TopScreen.X - (Width * 0.5);
            const double Top = std::min(TopScreen.Y, BottomScreen.Y);

            const FLinearColor Color = IsPlayer
                ? MakeColor(0.20F, 0.75F, 1.0F)
                : MakeColor(1.0F, 0.30F, 0.20F);

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

        const ClassDescriptor& Descriptor = DescribeActorClass(Actor, Roots);
        if (Descriptor.Category == MarkerCategory::None ||
            !IsCategoryEnabled(Descriptor.Category, Descriptor.Ammo))
        {
            return;
        }

        const FVector Location = Actor->K2_GetActorLocation();
        const double Distance = LocalPawn ? DistanceMeters(LocalLocation, Location) : 0.0;
        if (LocalPawn && Distance > g_Settings.RadiusMeters)
            return;

        const std::size_t CategoryIndex = static_cast<std::size_t>(Descriptor.Category);
        if (CategoryIndex < CategoryCounts.size())
            ++CategoryCounts[CategoryIndex];

        Markers.push_back(MarkerCandidate{Location, Distance, Descriptor});
    };

    const auto ProcessLevel = [&](ULevel* Level)
    {
        if (!Level)
            return;

        const auto& Actors = Level->Actors;
        const int32 ActorCount = Actors.Num();
        if (ActorCount <= 0 || ActorCount > 200000)
            return;

        for (int32 Index = 0; Index < ActorCount && ScannedActors < 500000; ++Index)
        {
            ++ScannedActors;
            ProcessActor(Actors[Index]);
        }
    };

    const int32 LevelCount = World->Levels.Num();
    if (LevelCount > 0 && LevelCount <= 512)
    {
        for (int32 LevelIndex = 0; LevelIndex < LevelCount && ScannedActors < 500000; ++LevelIndex)
            ProcessLevel(World->Levels[LevelIndex]);
    }
    else
    {
        ProcessLevel(World->PersistentLevel);
    }

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

    if (g_Settings.ShowStatus)
    {
        std::wostringstream Status;
        Status << L"Offline inspector  |  Players " << VisiblePlayers
               << L"  Zombies " << VisibleZombies
               << L"  Loot " << LootCount
               << L"  World " << WorldCount
               << L"  Markers " << MarkerLimit << L"/" << Markers.size()
               << L"  Radius " << std::fixed << std::setprecision(0) << g_Settings.RadiusMeters << L"m"
               << L"  |  F1 menu  Insert toggle  End unload";
        DrawText(Hud, Status.str(), 20.0F, 20.0F, MakeColor(0.85F, 0.95F, 1.0F), 0.85F);
    }

    RenderLegend(Hud);
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

    if ((GetAsyncKeyState(VK_INSERT) & 1) != 0)
        g_OverlayEnabled.store(!g_OverlayEnabled.load(std::memory_order_relaxed), std::memory_order_relaxed);

    if (!g_OverlayEnabled.load(std::memory_order_relaxed))
        return;

    HandleMenuInput();

    IsDrawing = true;
    RenderOfflineOverlay(static_cast<AHUD*>(const_cast<UObject*>(Object)));
    IsDrawing = false;
}

bool PatchHudProcessEvent(AHUD* Hud)
{
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

    while (g_Running.load(std::memory_order_relaxed))
    {
        if ((GetAsyncKeyState(VK_END) & 1) != 0)
        {
            g_Running.store(false, std::memory_order_relaxed);
            break;
        }

        UWorld* World = UWorld::GetWorld();
        if (APlayerController* Controller = GetLocalController(World))
            PatchHudProcessEvent(Controller->MyHUD);

        Sleep(kHookPollMilliseconds);
    }

    RestoreHooks();

    // Once the vtable slots are restored, no new hook calls can begin. Give
    // any frame already inside the hook time to finish before unloading.
    for (int Attempt = 0; Attempt < 1000 && g_ActiveHookCalls.load(std::memory_order_acquire) != 0; ++Attempt)
        Sleep(10);

    if (g_ActiveHookCalls.load(std::memory_order_acquire) != 0)
        return 0;

    g_OriginalProcessEvent.store(nullptr, std::memory_order_release);
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
