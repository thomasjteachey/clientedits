-- All-Star Lives (92013): hidden, death and arena persistent, stackable to 10.
-- A clone of the 92011 Tournament Character marker (Centurion Spell.dbc) with
-- StackAmount set; the stack count is the lives left (Arena::ShowAllStarLives),
-- read by the name-tag DLL. Loaded at startup (LOAD_DBC_EXT spell_dbc).
-- Run against the world database (no schema prefix).
DELETE FROM spell_dbc WHERE Id = 92013;
INSERT INTO spell_dbc (Id, Attributes, AttributesEx, AttributesEx2, AttributesEx3, AttributesEx4, AttributesEx6,
    CastingTimeIndex, ProcChance, DurationIndex, RangeIndex, StackAmount, EquippedItemClass,
    Effect1, EffectDieSides1, EffectImplicitTargetA1, EffectApplyAuraName1,
    DmgMultiplier1, DmgMultiplier2, DmgMultiplier3, SchoolMask, SpellName)
VALUES (92013, 2147483648, 301989888, 4, 1048576, 2097152, 4,
    1, 101, 21, 13, 10, -1,
    6, 1, 25, 4,
    1, 1, 1, 64, 'All-Star Lives');
