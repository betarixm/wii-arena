from dataclasses import dataclass, field
from enum import Enum


class RaceChoice(Enum):
    SOLO_RACE = "Solo Race"
    TEAM_RACE = "Team Race"


class CharacterChoice(Enum):
    BABY_MARIO = "Baby Mario"
    BABY_LUIGI = "Baby Luigi"
    BABY_PEACH = "Baby Peach"
    BABY_DAISY = "Baby Daisy"
    TOAD = "Toad"
    TOADETTE = "Toadette"
    KOOPA_TROOPA = "Koopa Troopa"
    DRY_BONES = "Dry Bones"
    MARIO = "Mario"
    LUIGI = "Luigi"
    PEACH = "Peach"
    DAISY = "Daisy"
    YOSHI = "Yoshi"
    BIRDO = "Birdo"
    DIDDY_KONG = "Diddy Kong"
    BOWSER_JR = "Bowser Jr"
    WARIO = "Wario"
    WALUIGI = "Waluigi"
    DONKEY_KONG = "Donkey Kong"
    BOWSER = "Bowser"
    KING_BOO = "King Boo"
    ROSALINA = "Rosalina"
    FUNKY_KONG = "Funky Kong"
    DRY_BOWSER = "Dry Bowser"
    MII_A = "Mii A"
    MII_B = "Mii B"


CHARACTER_POSITION_MAP: dict[CharacterChoice, tuple[int, int]] = {
    CharacterChoice.BABY_MARIO: (0, 0),
    CharacterChoice.BABY_LUIGI: (0, 1),
    CharacterChoice.BABY_PEACH: (0, 2),
    CharacterChoice.BABY_DAISY: (0, 3),
    CharacterChoice.TOAD: (1, 0),
    CharacterChoice.TOADETTE: (1, 1),
    CharacterChoice.KOOPA_TROOPA: (1, 2),
    CharacterChoice.DRY_BONES: (1, 3),
    CharacterChoice.MARIO: (2, 0),
    CharacterChoice.LUIGI: (2, 1),
    CharacterChoice.PEACH: (2, 2),
    CharacterChoice.DAISY: (2, 3),
    CharacterChoice.YOSHI: (3, 0),
    CharacterChoice.BIRDO: (3, 1),
    CharacterChoice.DIDDY_KONG: (3, 2),
    CharacterChoice.BOWSER_JR: (3, 3),
    CharacterChoice.WARIO: (4, 0),
    CharacterChoice.WALUIGI: (4, 1),
    CharacterChoice.DONKEY_KONG: (4, 2),
    CharacterChoice.BOWSER: (4, 3),
    CharacterChoice.KING_BOO: (5, 0),
    CharacterChoice.ROSALINA: (5, 1),
    CharacterChoice.FUNKY_KONG: (5, 2),
    CharacterChoice.DRY_BOWSER: (5, 3),
    CharacterChoice.MII_A: (6, 2),
    CharacterChoice.MII_B: (6, 3),
}


class VehicleType(Enum):
    KART = "kart"
    BIKE = "bike"


class VehicleSize(Enum):
    SMALL = "small"
    MEDIUM = "medium"
    LARGE = "large"


class VehicleChoice(Enum):
    STANDARD_KART_S = "Standard Kart S"
    STANDARD_BIKE_S = "Standard Bike S"
    BABY_BOOSTER = "Baby Booster"
    BULLET_BIKE = "Bullet Bike"
    CONCERTO = "Concerto"
    NANOBIKE = "Nanobike"
    STANDARD_KART_M = "Standard Kart M"
    STANDARD_BIKE_M = "Standard Bike M"
    NOSTALGIA_1 = "Nostalgia 1"
    MACH_BIKE = "Mach Bike"
    WILD_WING = "Wild Wing"
    BON_BON = "Bon Bon"
    STANDARD_KART_L = "Standard Kart L"
    STANDARD_BIKE_L = "Standard Bike L"
    OFFROADER = "Offroader"
    BOWSER_BIKE = "Bowser Bike"
    FLAME_FLYER = "Flame Flyer"
    WARIO_BIKE = "Wario Bike"
    CHEEP_CHARGER = "Cheep Charger"
    QUACKER = "Quacker"
    RALLY_ROMPER = "Rally Romper"
    MAGIKRUISER = "Magikruiser"
    BLUE_FALCON = "Blue Falcon"
    BUBBLE_BIKE = "Bubble Bike"
    TURBO_BLOOPER = "Turbo Blooper"
    RAPIDE = "Rapide"
    ROYAL_RACER = "Royal Racer"
    NITROCYCLE = "Nitrocycle"
    B_DASHER_MK_2 = "B Dasher MK 2"
    DOLPHIN_DASHER = "Dolphin Dasher"
    PIRANHA_PROWLER = "Piranha Prowler"
    TWINKLE_STAR = "Twinkle Star"
    AERO_GLIDER = "Aero Glider"
    TORPEDO = "Torpedo"
    DRAGONETTI = "Dragonetti"
    PHANTOM = "Phantom"


_VEHICLE_CHOICE_GRID: dict[
    VehicleSize, dict[VehicleType, tuple[tuple[VehicleChoice, ...], ...]]
] = {
    VehicleSize.SMALL: {
        VehicleType.KART: (
            (
                VehicleChoice.STANDARD_KART_S,
                VehicleChoice.BABY_BOOSTER,
                VehicleChoice.CONCERTO,
            ),
            (
                VehicleChoice.CHEEP_CHARGER,
                VehicleChoice.RALLY_ROMPER,
                VehicleChoice.BLUE_FALCON,
            ),
        ),
        VehicleType.BIKE: (
            (
                VehicleChoice.STANDARD_BIKE_S,
                VehicleChoice.BULLET_BIKE,
                VehicleChoice.NANOBIKE,
            ),
            (
                VehicleChoice.QUACKER,
                VehicleChoice.MAGIKRUISER,
                VehicleChoice.BUBBLE_BIKE,
            ),
        ),
    },
    VehicleSize.MEDIUM: {
        VehicleType.KART: (
            (
                VehicleChoice.STANDARD_KART_M,
                VehicleChoice.NOSTALGIA_1,
                VehicleChoice.WILD_WING,
            ),
            (
                VehicleChoice.TURBO_BLOOPER,
                VehicleChoice.B_DASHER_MK_2,
                VehicleChoice.ROYAL_RACER,
            ),
        ),
        VehicleType.BIKE: (
            (
                VehicleChoice.STANDARD_BIKE_M,
                VehicleChoice.MACH_BIKE,
                VehicleChoice.BON_BON,
            ),
            (
                VehicleChoice.RAPIDE,
                VehicleChoice.NITROCYCLE,
                VehicleChoice.DOLPHIN_DASHER,
            ),
        ),
    },
    VehicleSize.LARGE: {
        VehicleType.KART: (
            (
                VehicleChoice.STANDARD_KART_L,
                VehicleChoice.OFFROADER,
                VehicleChoice.FLAME_FLYER,
            ),
            (
                VehicleChoice.PIRANHA_PROWLER,
                VehicleChoice.AERO_GLIDER,
                VehicleChoice.DRAGONETTI,
            ),
        ),
        VehicleType.BIKE: (
            (
                VehicleChoice.STANDARD_BIKE_L,
                VehicleChoice.BOWSER_BIKE,
                VehicleChoice.WARIO_BIKE,
            ),
            (VehicleChoice.TWINKLE_STAR, VehicleChoice.TORPEDO, VehicleChoice.PHANTOM),
        ),
    },
}


VEHICLE_CHOICE_QUEUE: dict[VehicleSize, list[VehicleChoice]] = {
    VehicleSize.SMALL: [
        VehicleChoice.STANDARD_KART_S,
        VehicleChoice.BABY_BOOSTER,
        VehicleChoice.CONCERTO,
        VehicleChoice.CHEEP_CHARGER,
        VehicleChoice.RALLY_ROMPER,
        VehicleChoice.BLUE_FALCON,
        VehicleChoice.STANDARD_BIKE_S,
        VehicleChoice.BULLET_BIKE,
        VehicleChoice.NANOBIKE,
        VehicleChoice.QUACKER,
        VehicleChoice.MAGIKRUISER,
        VehicleChoice.BUBBLE_BIKE,
    ],
    VehicleSize.MEDIUM: [
        VehicleChoice.STANDARD_KART_M,
        VehicleChoice.NOSTALGIA_1,
        VehicleChoice.WILD_WING,
        VehicleChoice.TURBO_BLOOPER,
        VehicleChoice.ROYAL_RACER,
        VehicleChoice.B_DASHER_MK_2,
        VehicleChoice.STANDARD_BIKE_M,
        VehicleChoice.MACH_BIKE,
        VehicleChoice.BON_BON,
        VehicleChoice.RAPIDE,
        VehicleChoice.NITROCYCLE,
        VehicleChoice.DOLPHIN_DASHER,
    ],
    VehicleSize.LARGE: [
        VehicleChoice.STANDARD_KART_L,
        VehicleChoice.OFFROADER,
        VehicleChoice.FLAME_FLYER,
        VehicleChoice.PIRANHA_PROWLER,
        VehicleChoice.AERO_GLIDER,
        VehicleChoice.DRAGONETTI,
        VehicleChoice.STANDARD_BIKE_L,
        VehicleChoice.BOWSER_BIKE,
        VehicleChoice.WARIO_BIKE,
        VehicleChoice.TWINKLE_STAR,
        VehicleChoice.TORPEDO,
        VehicleChoice.PHANTOM,
    ],
}

_VEHICLE_SIZE_MAP: dict[VehicleChoice, VehicleSize] = {
    vehicle: size
    for size, vehicles in VEHICLE_CHOICE_QUEUE.items()
    for vehicle in vehicles
}


def _build_vehicle_position_map() -> dict[VehicleChoice, tuple[int, int]]:
    result: dict[VehicleChoice, tuple[int, int]] = {}
    for size_grid in _VEHICLE_CHOICE_GRID.values():
        for ui_col, vehicle_type in ((0, VehicleType.KART), (1, VehicleType.BIKE)):
            for block, column in enumerate(size_grid[vehicle_type]):
                for row, vehicle in enumerate(column):
                    result[vehicle] = (row + block * 3, ui_col)
    return result


VEHICLE_POSITION_MAP: dict[VehicleChoice, tuple[int, int]] = (
    _build_vehicle_position_map()
)


class DriftModeChoice(Enum):
    AUTOMATIC = "Automatic"
    MANUAL = "Manual"


class CupChoice(Enum):
    MUSHROOM_CUP = "Mushroom Cup"
    FLOWER_CUP = "Flower Cup"
    STAR_CUP = "Star Cup"
    SPECIAL_CUP = "Special Cup"
    SHELL_CUP = "Shell Cup"
    BANANA_CUP = "Banana Cup"
    LEAF_CUP = "Leaf Cup"
    LIGHTNING_CUP = "Lightning Cup"


CUP_POSITION_MAP: dict[CupChoice, tuple[int, int]] = {
    CupChoice.MUSHROOM_CUP: (0, 0),
    CupChoice.FLOWER_CUP: (0, 1),
    CupChoice.STAR_CUP: (0, 2),
    CupChoice.SPECIAL_CUP: (0, 3),
    CupChoice.SHELL_CUP: (1, 0),
    CupChoice.BANANA_CUP: (1, 1),
    CupChoice.LEAF_CUP: (1, 2),
    CupChoice.LIGHTNING_CUP: (1, 3),
}


class CourseChoice(Enum):
    LUIGI_CIRCUIT = "Luigi Circuit"
    MOO_MOO_MEADOWS = "Moo Moo Meadows"
    MUSHROOM_GORGE = "Mushroom Gorge"
    TOADS_FACTORY = "Toad's Factory"

    MARIO_CIRCUIT = "Mario Circuit"
    COCONUT_MALL = "Coconut Mall"
    DK_SUMMIT = "DK Summit"
    WARIOS_GOLD_MINE = "Wario's Gold Mine"

    DAISY_CIRCUIT = "Daisy Circuit"
    KOOPA_CAPE = "Koopa Cape"
    MAPLE_TREEWAY = "Maple Treeway"
    GRUMBLE_VOLCANO = "Grumble Volcano"

    DRY_DRY_RUINS = "Dry Dry Ruins"
    MOONVIEW_HIGHWAY = "Moonview Highway"
    BOWSERS_CASTLE = "Bowser's Castle"
    RAINBOW_ROAD = "Rainbow Road"

    GCN_PEACH_BEACH = "GCN Peach Beach"
    DS_YOSHI_FALLS = "DS Yoshi Falls"
    SNES_GHOST_VALLEY_2 = "SNES Ghost Valley 2"
    N64_MARIO_RACEWAY = "N64 Mario Raceway"

    N64_SHERBET_LAND = "N64 Sherbet Land"
    GBA_SHY_GUY_BEACH = "GBA Shy Guy Beach"
    DS_DELFINO_SQUARE = "DS Delfino Square"
    GCN_WALUIGI_STADIUM = "GCN Waluigi Stadium"

    DS_DESERT_STREET = "DS Desert Street"
    GBA_BOWSER_CASTLE_3 = "GBA Bowser Castle 3"
    N64_DKS_JUNGLE_PARKWAY = "N64 DK's Jungle Parkway"
    GCN_MARIO_CIRCUIT = "GCN Mario Circuit"

    SNES_MARIO_CIRCUIT_3 = "SNES Mario Circuit 3"
    DS_PEACH_GARDENS = "DS Peach Gardens"
    GCN_DK_MOUNTAIN = "GCN DK Mountain"
    N64_BOWSERS_CASTLE = "N64 Bowser's Castle"


COURSE_POSITION_MAP: dict[CourseChoice, int] = {
    CourseChoice.LUIGI_CIRCUIT: 0,
    CourseChoice.MOO_MOO_MEADOWS: 1,
    CourseChoice.MUSHROOM_GORGE: 2,
    CourseChoice.TOADS_FACTORY: 3,
    CourseChoice.MARIO_CIRCUIT: 0,
    CourseChoice.COCONUT_MALL: 1,
    CourseChoice.DK_SUMMIT: 2,
    CourseChoice.WARIOS_GOLD_MINE: 3,
    CourseChoice.DAISY_CIRCUIT: 0,
    CourseChoice.KOOPA_CAPE: 1,
    CourseChoice.MAPLE_TREEWAY: 2,
    CourseChoice.GRUMBLE_VOLCANO: 3,
    CourseChoice.DRY_DRY_RUINS: 0,
    CourseChoice.MOONVIEW_HIGHWAY: 1,
    CourseChoice.BOWSERS_CASTLE: 2,
    CourseChoice.RAINBOW_ROAD: 3,
    CourseChoice.GCN_PEACH_BEACH: 0,
    CourseChoice.DS_YOSHI_FALLS: 1,
    CourseChoice.SNES_GHOST_VALLEY_2: 2,
    CourseChoice.N64_MARIO_RACEWAY: 3,
    CourseChoice.N64_SHERBET_LAND: 0,
    CourseChoice.GBA_SHY_GUY_BEACH: 1,
    CourseChoice.DS_DELFINO_SQUARE: 2,
    CourseChoice.GCN_WALUIGI_STADIUM: 3,
    CourseChoice.DS_DESERT_STREET: 0,
    CourseChoice.GBA_BOWSER_CASTLE_3: 1,
    CourseChoice.N64_DKS_JUNGLE_PARKWAY: 2,
    CourseChoice.GCN_MARIO_CIRCUIT: 3,
    CourseChoice.SNES_MARIO_CIRCUIT_3: 0,
    CourseChoice.DS_PEACH_GARDENS: 1,
    CourseChoice.GCN_DK_MOUNTAIN: 2,
    CourseChoice.N64_BOWSERS_CASTLE: 3,
}

COURSE_TO_CUP_MAP: dict[CourseChoice, CupChoice] = {
    CourseChoice.LUIGI_CIRCUIT: CupChoice.MUSHROOM_CUP,
    CourseChoice.MOO_MOO_MEADOWS: CupChoice.MUSHROOM_CUP,
    CourseChoice.MUSHROOM_GORGE: CupChoice.MUSHROOM_CUP,
    CourseChoice.TOADS_FACTORY: CupChoice.MUSHROOM_CUP,
    CourseChoice.MARIO_CIRCUIT: CupChoice.FLOWER_CUP,
    CourseChoice.COCONUT_MALL: CupChoice.FLOWER_CUP,
    CourseChoice.DK_SUMMIT: CupChoice.FLOWER_CUP,
    CourseChoice.WARIOS_GOLD_MINE: CupChoice.FLOWER_CUP,
    CourseChoice.DAISY_CIRCUIT: CupChoice.STAR_CUP,
    CourseChoice.KOOPA_CAPE: CupChoice.STAR_CUP,
    CourseChoice.MAPLE_TREEWAY: CupChoice.STAR_CUP,
    CourseChoice.GRUMBLE_VOLCANO: CupChoice.STAR_CUP,
    CourseChoice.DRY_DRY_RUINS: CupChoice.SPECIAL_CUP,
    CourseChoice.MOONVIEW_HIGHWAY: CupChoice.SPECIAL_CUP,
    CourseChoice.BOWSERS_CASTLE: CupChoice.SPECIAL_CUP,
    CourseChoice.RAINBOW_ROAD: CupChoice.SPECIAL_CUP,
    CourseChoice.GCN_PEACH_BEACH: CupChoice.SHELL_CUP,
    CourseChoice.DS_YOSHI_FALLS: CupChoice.SHELL_CUP,
    CourseChoice.SNES_GHOST_VALLEY_2: CupChoice.SHELL_CUP,
    CourseChoice.N64_MARIO_RACEWAY: CupChoice.SHELL_CUP,
    CourseChoice.N64_SHERBET_LAND: CupChoice.BANANA_CUP,
    CourseChoice.GBA_SHY_GUY_BEACH: CupChoice.BANANA_CUP,
    CourseChoice.DS_DELFINO_SQUARE: CupChoice.BANANA_CUP,
    CourseChoice.GCN_WALUIGI_STADIUM: CupChoice.BANANA_CUP,
    CourseChoice.DS_DESERT_STREET: CupChoice.LEAF_CUP,
    CourseChoice.GBA_BOWSER_CASTLE_3: CupChoice.LEAF_CUP,
    CourseChoice.N64_DKS_JUNGLE_PARKWAY: CupChoice.LEAF_CUP,
    CourseChoice.GCN_MARIO_CIRCUIT: CupChoice.LEAF_CUP,
    CourseChoice.SNES_MARIO_CIRCUIT_3: CupChoice.LIGHTNING_CUP,
    CourseChoice.DS_PEACH_GARDENS: CupChoice.LIGHTNING_CUP,
    CourseChoice.GCN_DK_MOUNTAIN: CupChoice.LIGHTNING_CUP,
    CourseChoice.N64_BOWSERS_CASTLE: CupChoice.LIGHTNING_CUP,
}


class CCChoice(Enum):
    CC_50 = "50cc"
    CC_100 = "100cc"
    CC_150 = "150cc"
    MIRROR = "mirror"


class CPUChoice(Enum):
    EASY = "Easy"
    NORMAL = "Normal"
    HARD = "Hard"
    OFF = "Off"


class VehicleRuleChoice(Enum):
    ALL = "All"
    KARTS = "Karts"
    BIKES = "Bikes"


class CourseRuleChoice(Enum):
    CHOOSE = "Choose"
    RANDOM = "Random"
    IN_ORDER = "In Order"


class ItemRuleChoice(Enum):
    RECOMMENDED = "Recommended"
    FRANTIC = "Frantic"
    BASIC = "Basic"
    NONE = "None"


class RacesChoice(Enum):
    RACES_2 = 2
    RACES_3 = 3
    RACES_4 = 4
    RACES_5 = 5
    RACES_8 = 8
    RACES_10 = 10
    RACES_12 = 12
    RACES_16 = 16
    RACES_32 = 32


_RULES_GRID: tuple[tuple[Enum, ...], ...] = (
    (
        CCChoice.CC_50,
        CCChoice.CC_100,
        CCChoice.CC_150,
        CCChoice.MIRROR,
    ),
    (
        CPUChoice.EASY,
        CPUChoice.NORMAL,
        CPUChoice.HARD,
        CPUChoice.OFF,
    ),
    (
        VehicleRuleChoice.ALL,
        VehicleRuleChoice.KARTS,
        VehicleRuleChoice.BIKES,
    ),
    (
        CourseRuleChoice.CHOOSE,
        CourseRuleChoice.RANDOM,
        CourseRuleChoice.IN_ORDER,
    ),
    (
        ItemRuleChoice.RECOMMENDED,
        ItemRuleChoice.FRANTIC,
        ItemRuleChoice.BASIC,
        ItemRuleChoice.NONE,
    ),
    (
        RacesChoice.RACES_2,
        RacesChoice.RACES_3,
        RacesChoice.RACES_4,
        RacesChoice.RACES_5,
        RacesChoice.RACES_8,
        RacesChoice.RACES_10,
        RacesChoice.RACES_12,
        RacesChoice.RACES_16,
        RacesChoice.RACES_32,
    ),
)

RULES_COLUMN_MAP: dict[Enum, int] = {
    choice: col for row in _RULES_GRID for col, choice in enumerate(row)
}


def get_character_size(character_choice: CharacterChoice) -> VehicleSize:
    row, _ = CHARACTER_POSITION_MAP[character_choice]
    if row <= 1:
        return VehicleSize.SMALL
    if row <= 3:
        return VehicleSize.MEDIUM
    if row <= 5:
        return VehicleSize.LARGE
    raise ValueError(
        "Mii character size cannot be inferred from row index. "
        "Use a non-Mii character or extend this macro with explicit Mii size handling."
    )


@dataclass
class MarioKartWiiOptions:
    num_agents: int = 4
    race: RaceChoice = RaceChoice.SOLO_RACE
    character: list[CharacterChoice] = field(default_factory=list[CharacterChoice])
    vehicle: list[VehicleChoice] = field(default_factory=list[VehicleChoice])
    drift_modes: list[DriftModeChoice] = field(default_factory=list[DriftModeChoice])
    course: CourseChoice = CourseChoice.LUIGI_CIRCUIT
    cc: CCChoice = CCChoice.CC_100
    cpu: CPUChoice = CPUChoice.NORMAL
    vehicle_rule: VehicleRuleChoice = VehicleRuleChoice.ALL
    course_rule: CourseRuleChoice = CourseRuleChoice.CHOOSE
    item_rule: ItemRuleChoice = ItemRuleChoice.RECOMMENDED
    races: RacesChoice = RacesChoice.RACES_4

    def __post_init__(self) -> None:
        if not self.vehicle:
            self.vehicle = [VehicleChoice.BON_BON] * self.num_agents
        if not self.drift_modes:
            self.drift_modes = [DriftModeChoice.MANUAL] * self.num_agents

        if self.num_agents == 1:
            if not self.character:
                self.character = [
                    CharacterChoice.MARIO,
                ]
        elif self.num_agents == 4:
            if not self.character:
                self.character = [
                    CharacterChoice.MARIO,
                    CharacterChoice.LUIGI,
                    CharacterChoice.YOSHI,
                    CharacterChoice.PEACH,
                ]
        else:
            raise ValueError(
                f"num_agents should be 1 or 4, but it is {self.num_agents}"
            )

        if (
            not self.num_agents
            == len(self.character)
            == len(self.vehicle)
            == len(self.drift_modes)
        ):
            raise ValueError(
                "Length of character, vehicle, and drift_modes lists must match num_agents. "
                f"{self.num_agents=}, "
                f"{len(self.character)=}, "
                f"{len(self.vehicle)=}, "
                f"{len(self.drift_modes)=}."
            )
        for i in range(self.num_agents):
            selected_vehicle = self.vehicle[i]
            selected_character = self.character[i]
            vehicle_size = _VEHICLE_SIZE_MAP[selected_vehicle]
            target_size = get_character_size(selected_character)

            if vehicle_size is not target_size:
                raise ValueError(
                    f"{selected_vehicle.value} is {vehicle_size.value} size, "
                    f"but {selected_character.value} is {target_size.value} size."
                )
