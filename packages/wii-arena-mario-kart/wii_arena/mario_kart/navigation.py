import logging

from wii_arena.dolphin import Dolphin, DolphinAgentAction, DolphinAgentIndex

from .options import (
    CHARACTER_POSITION_MAP,
    COURSE_POSITION_MAP,
    COURSE_TO_CUP_MAP,
    CUP_POSITION_MAP,
    RULES_COLUMN_MAP,
    VEHICLE_CHOICE_QUEUE,
    VEHICLE_POSITION_MAP,
    DriftModeChoice,
    MarioKartWiiOptions,
    RaceChoice,
    get_character_size,
)

_LOGGER = logging.getLogger(__name__)

_A = DolphinAgentAction(a=True)
_UP = DolphinAgentAction(up=True)
_DOWN = DolphinAgentAction(down=True)
_LEFT = DolphinAgentAction(left=True)
_RIGHT = DolphinAgentAction(right=True)

_P1 = DolphinAgentIndex(1)
_P2 = DolphinAgentIndex(2)
_P3 = DolphinAgentIndex(3)
_P4 = DolphinAgentIndex(4)


def navigate(session: Dolphin.Session, options: MarioKartWiiOptions) -> None:
    _LOGGER.info(
        "Navigating menus for num_agents=%d, course=%s",
        options.num_agents,
        options.course.value,
    )
    enter_main_menu(session, options)

    if options.num_agents == 1:
        session.click({_P1: _A}, idle_frames=500)
        session.click({_P1: _DOWN}, idle_frames=10)
        session.click({_P1: _DOWN}, idle_frames=10)
        session.click(
            {_P1: _A}, idle_frames=100
        )  # select VS Race in ["VS Race", "Battle"]
    else:
        for _ in range(3):
            session.click({_P1: _RIGHT}, idle_frames=10)
        session.click({_P1: _A})

        session.click({_P2: _A}, idle_frames=10)
        session.click({_P3: _A}, idle_frames=10)
        session.click({_P4: _A}, idle_frames=10)
        session.click({}, idle_frames=50)
        session.click({_P1: _A}, idle_frames=100)

        session.click(
            {_P1: _A}, idle_frames=100
        )  # select VS Race in ["VS Race", "Battle"]

    # setting rules (CC, CPU, etc.)
    session.click({_P1: _UP}, idle_frames=10)
    session.click({_P1: _A}, idle_frames=100)
    select_rules(session, options)
    session.click({_P1: _DOWN}, idle_frames=10)

    if options.race == RaceChoice.SOLO_RACE:
        session.click({_P1: _A})
    elif options.race == RaceChoice.TEAM_RACE:
        session.click({_P1: _DOWN}, idle_frames=10)
        session.click({_P1: _A})

    select_character(session, options)

    if options.race == RaceChoice.TEAM_RACE:
        if options.num_agents == 1:
            # Team select
            session.click({_P1: _A})
        else:
            # Team select (1, 3p select red team, 2, 4p select green team)
            session.click({_P1: _A, _P2: _A, _P3: _A, _P4: _A})
            session.click({_P1: _A})

    select_vehicle(session, options)

    if options.num_agents == 1:
        if options.drift_modes[0] == DriftModeChoice.AUTOMATIC:
            session.click({_P1: _UP}, idle_frames=10)
            session.click({_P1: _A}, idle_frames=10)
        elif options.drift_modes[0] == DriftModeChoice.MANUAL:
            session.click({_P1: _A}, idle_frames=10)
    else:
        for player in range(1, options.num_agents + 1):
            index = DolphinAgentIndex(player)
            if options.drift_modes[player - 1] == DriftModeChoice.AUTOMATIC:
                session.click({index: _A}, idle_frames=10)
            elif options.drift_modes[player - 1] == DriftModeChoice.MANUAL:
                session.click({index: _DOWN}, idle_frames=10)
                session.click({index: _A}, idle_frames=10)
    session.click({})

    select_cup(session, options)

    select_course(session, options)

    session.click({}, idle_frames=1250)


def enter_main_menu(session: Dolphin.Session, options: MarioKartWiiOptions) -> None:
    all_a = {
        DolphinAgentIndex(player): _A for player in range(1, options.num_agents + 1)
    }
    session.click({}, idle_frames=500)
    session.click(all_a, idle_frames=500)

    for _ in range(7):
        session.click(all_a)


def select_character(session: Dolphin.Session, options: MarioKartWiiOptions) -> None:
    if options.num_agents == 4:
        session.click({_P4: _DOWN}, idle_frames=10)
        session.click({_P4: _DOWN}, idle_frames=10)
        session.click({_P4: _DOWN}, idle_frames=10)
        session.click({_P4: _DOWN}, idle_frames=10)
        session.click({_P4: _RIGHT}, idle_frames=10)

        session.click({_P3: _RIGHT}, idle_frames=10)
        session.click({_P3: _RIGHT}, idle_frames=10)
        session.click({_P3: _DOWN}, idle_frames=10)
        session.click({_P3: _DOWN}, idle_frames=10)
        session.click({_P3: _DOWN}, idle_frames=10)
        session.click({_P3: _RIGHT}, idle_frames=10)

        session.click({_P2: _RIGHT}, idle_frames=10)
        session.click({_P2: _DOWN}, idle_frames=10)
        session.click({_P2: _DOWN}, idle_frames=10)
        session.click({_P2: _DOWN}, idle_frames=10)
        session.click({_P2: _DOWN}, idle_frames=10)
        session.click({_P2: _RIGHT}, idle_frames=10)

    session.click({_P1: _RIGHT}, idle_frames=10)
    session.click({_P1: _RIGHT}, idle_frames=10)
    session.click({_P1: _DOWN}, idle_frames=10)
    session.click({_P1: _DOWN}, idle_frames=10)
    session.click({_P1: _DOWN}, idle_frames=10)
    session.click({_P1: _DOWN}, idle_frames=10)
    session.click({_P1: _RIGHT}, idle_frames=10)

    selected_coordinate: list[tuple[int, int]] = []
    selected_choices: list[tuple[int, int]] = []
    for player in range(1, options.num_agents + 1):
        coordinate = CHARACTER_POSITION_MAP[options.character[player - 1]]
        selected_coordinate.append(coordinate)
        selected_choices.append((player, coordinate[0] * 4 + coordinate[1]))

    for player, _ in sorted(selected_choices, key=lambda x: x[1]):
        index = DolphinAgentIndex(player)
        row, col = selected_coordinate[player - 1]
        for _ in range(6 - row):
            session.click({index: _UP}, idle_frames=10)
        for _ in range(3 - col):
            session.click({index: _LEFT}, idle_frames=10)
        session.click({index: _A}, idle_frames=10)

    session.click({})


def select_vehicle(session: Dolphin.Session, options: MarioKartWiiOptions) -> None:
    is_grid = options.num_agents == 1
    for player in range(1, options.num_agents + 1):
        index = DolphinAgentIndex(player)
        selected_vehicle = options.vehicle[player - 1]

        if is_grid:
            target_row, target_col = VEHICLE_POSITION_MAP[selected_vehicle]

            vertical_move = _UP if target_row <= 0 else _DOWN
            for _ in range(abs(target_row)):
                session.click({index: vertical_move}, idle_frames=10)

            horizontal_move = _LEFT if target_col <= 0 else _RIGHT
            for _ in range(abs(target_col)):
                session.click({index: horizontal_move}, idle_frames=10)
            session.click({index: _A}, idle_frames=150)

        else:
            target_size = get_character_size(options.character[player - 1])
            for vehicle in VEHICLE_CHOICE_QUEUE[target_size]:
                session.click({index: DolphinAgentAction()}, idle_frames=10)
                if vehicle == selected_vehicle:
                    session.click({index: _A}, idle_frames=150)
                    break
                session.click({index: _RIGHT}, idle_frames=10)


def select_cup(session: Dolphin.Session, options: MarioKartWiiOptions) -> None:
    target_row, target_col = CUP_POSITION_MAP[COURSE_TO_CUP_MAP[options.course]]

    for _ in range(target_row):
        session.click({_P1: _DOWN}, idle_frames=10)

    for _ in range(target_col):
        session.click({_P1: _RIGHT}, idle_frames=10)

    session.click({_P1: _A}, idle_frames=100)


def select_course(session: Dolphin.Session, options: MarioKartWiiOptions) -> None:
    target_index = COURSE_POSITION_MAP[options.course]
    for _ in range(target_index):
        session.click({_P1: _DOWN}, idle_frames=10)
    session.click({_P1: _A}, idle_frames=100)
    session.click({_P1: _A})


def select_rules(session: Dolphin.Session, options: MarioKartWiiOptions) -> None:
    rules = [
        (options.cc, 1),
        (options.cpu, 1),
        (options.vehicle_rule, 0),
        (options.course_rule, 0),
        (options.item_rule, 0),
        (options.races, 2),
    ]

    for selected_rule, start_col in rules:
        target_col = RULES_COLUMN_MAP[selected_rule]
        col_shift = target_col - start_col

        horizontal_move = _LEFT if col_shift <= 0 else _RIGHT
        for _ in range(abs(col_shift)):
            session.click({_P1: horizontal_move}, idle_frames=50)
        session.click({_P1: _A}, idle_frames=50)

    session.click({_P1: _A}, idle_frames=100)
