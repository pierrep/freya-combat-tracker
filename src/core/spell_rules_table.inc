// Generated from data/srd/spells.json. Each row is a spell whose own
// effect deals damage or gives a condition.
struct SpellDice {
    int count;
    int sides;
    int flat;
    const char* type;
    const char* when;  // empty: rolled now. later, aside, zone, repeat: told in the effect
};

struct SpellGrant {
    const char* ids;
    const char* on;
    const char* until;
    bool saveEnds;
    const char* worsens;
    const char* endsOn;
    const char* ask;
    const char* tiedTo;
};

struct SpellCombatRule {
    const char* id;
    const char* name;
    int level;
    bool concentration;
    const char* delivery;  // attack, save, auto
    bool melee;
    const char* saveAbility;
    bool halfOnSuccess;
    bool halfOnMiss;
    bool area;
    bool allowSelf;
    bool repeatSameTarget;
    int strikes;
    int strikesPerSlot;
    const char* advantageIf;
    const char* targetTypes;
    int hpThreshold;
    const char* below;  // dies, or empty when the grant applies only under the threshold
    const char* repeatSave;
    int upCount;
    int upSides;
    const char* upMode;  // all, initial, types
    const char* upTypes;
    const SpellDice* damage;
    int damageCount;
    const SpellGrant* grants;
    int grantCount;
    const char* note;
};

static const SpellDice kDiceAcidArrow[] = {
    {4, 4, 0, "acid", ""},
    {2, 4, 0, "acid", "later"},
};
static const SpellDice kDiceAcidSplash[] = {
    {1, 6, 0, "acid", ""},
};
static const SpellGrant kGrantAnimalFriendship[] = {
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantAntipathySympathy[] = {
    {"frightened", "failure", "", false, "", "", "", ""},
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceArcaneHand[] = {
    {5, 8, 0, "force", ""},
};
static const SpellGrant kGrantArcaneHand[] = {
    {"grappled", "hit", "", false, "", "", "", ""},
};
static const SpellDice kDiceArcaneSword[] = {
    {4, 12, 0, "force", ""},
};
static const SpellGrant kGrantAstralProjection[] = {
    {"unconscious", "cast", "", false, "", "", "", ""},
};
static const SpellGrant kGrantAwaken[] = {
    {"charmed", "cast", "", false, "", "", "", ""},
};
static const SpellGrant kGrantBanishment[] = {
    {"incapacitated", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceBefuddlement[] = {
    {10, 12, 0, "psychic", ""},
};
static const SpellDice kDiceBestowCurse[] = {
    {1, 8, 0, "necrotic", ""},
};
static const SpellDice kDiceBlackTentacles[] = {
    {3, 6, 0, "bludgeoning", ""},
};
static const SpellGrant kGrantBlackTentacles[] = {
    {"restrained", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceBladeBarrier[] = {
    {6, 10, 0, "force", ""},
};
static const SpellDice kDiceBlight[] = {
    {8, 8, 0, "necrotic", ""},
};
static const SpellGrant kGrantBlindnessDeafness[] = {
    {"blinded", "failure", "", true, "", "", "blinded", ""},
    {"deafened", "failure", "", true, "", "", "deafened", ""},
};
static const SpellDice kDiceBurningHands[] = {
    {3, 6, 0, "fire", ""},
};
static const SpellDice kDiceCallLightning[] = {
    {3, 10, 0, "lightning", ""},
};
static const SpellDice kDiceChainLightning[] = {
    {10, 8, 0, "lightning", ""},
};
static const SpellGrant kGrantCharmMonster[] = {
    {"charmed", "failure", "", false, "", "takesDamage", "", ""},
};
static const SpellGrant kGrantCharmPerson[] = {
    {"charmed", "failure", "", false, "", "takesDamage", "", ""},
};
static const SpellDice kDiceChillTouch[] = {
    {1, 10, 0, "necrotic", ""},
};
static const SpellDice kDiceChromaticOrb[] = {
    {3, 8, 0, "", ""},
};
static const SpellDice kDiceCircleOfDeath[] = {
    {8, 8, 0, "necrotic", ""},
};
static const SpellDice kDiceCloudkill[] = {
    {5, 8, 0, "poison", ""},
};
static const SpellGrant kGrantColorSpray[] = {
    {"blinded", "failure", "sourceEnd", false, "", "", "", ""},
};
static const SpellGrant kGrantCommand[] = {
    {"prone", "failure", "", false, "", "", "the command is Grovel", ""},
};
static const SpellGrant kGrantCompulsion[] = {
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceConeOfCold[] = {
    {8, 8, 0, "cold", ""},
};
static const SpellDice kDiceConjureAnimals[] = {
    {3, 10, 0, "slashing", ""},
};
static const SpellDice kDiceConjureCelestial[] = {
    {6, 12, 0, "radiant", ""},
};
static const SpellDice kDiceConjureElemental[] = {
    {8, 8, 0, "", ""},
    {4, 8, 0, "", "repeat"},
};
static const SpellGrant kGrantConjureElemental[] = {
    {"restrained", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceConjureFey[] = {
    {3, 12, 0, "psychic", ""},
};
static const SpellGrant kGrantConjureFey[] = {
    {"frightened", "hit", "sourceStart", false, "", "", "", ""},
};
static const SpellDice kDiceConjureWoodlandBeings[] = {
    {5, 8, 0, "force", ""},
};
static const SpellDice kDiceContactOtherPlane[] = {
    {6, 6, 0, "psychic", ""},
};
static const SpellGrant kGrantContactOtherPlane[] = {
    {"incapacitated", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceContagion[] = {
    {11, 8, 0, "necrotic", ""},
};
static const SpellGrant kGrantContagion[] = {
    {"poisoned", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceControlWater[] = {
    {2, 8, 0, "bludgeoning", ""},
};
static const SpellDice kDiceDisintegrate[] = {
    {10, 6, 40, "force", ""},
};
static const SpellDice kDiceDissonantWhispers[] = {
    {3, 6, 0, "psychic", ""},
};
static const SpellGrant kGrantDominateBeast[] = {
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantDominateMonster[] = {
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantDominatePerson[] = {
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceDragonsBreath[] = {
    {3, 6, 0, "", ""},
};
static const SpellDice kDiceDream[] = {
    {3, 6, 0, "psychic", ""},
};
static const SpellDice kDiceEarthquake[] = {
    {12, 6, 0, "bludgeoning", ""},
};
static const SpellGrant kGrantEarthquake[] = {
    {"prone", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceEldritchBlast[] = {
    {1, 10, 0, "force", ""},
};
static const SpellDice kDiceEnsnaringStrike[] = {
    {1, 6, 0, "piercing", ""},
};
static const SpellGrant kGrantEnsnaringStrike[] = {
    {"restrained", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantEntangle[] = {
    {"restrained", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantEyebite[] = {
    {"unconscious", "failure", "", false, "", "takesDamage", "", ""},
    {"frightened", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceFaithfulHound[] = {
    {4, 8, 0, "force", ""},
};
static const SpellGrant kGrantFear[] = {
    {"frightened", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceFingerOfDeath[] = {
    {7, 8, 30, "necrotic", ""},
};
static const SpellDice kDiceFireBolt[] = {
    {1, 10, 0, "fire", ""},
};
static const SpellDice kDiceFireStorm[] = {
    {7, 10, 0, "fire", ""},
};
static const SpellDice kDiceFireball[] = {
    {8, 6, 0, "fire", ""},
};
static const SpellDice kDiceFlameBlade[] = {
    {3, 6, 0, "fire", ""},
};
static const SpellDice kDiceFlameStrike[] = {
    {5, 6, 0, "fire", ""},
    {5, 6, 0, "radiant", ""},
};
static const SpellDice kDiceFlamingSphere[] = {
    {2, 6, 0, "fire", ""},
    {2, 6, 0, "fire", "zone"},
};
static const SpellGrant kGrantFleshToStone[] = {
    {"restrained", "failure", "", true, "", "", "", ""},
    {"petrified", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantFreedomOfMovement[] = {
    {"paralyzed", "cast", "", false, "", "", "paralyzed", ""},
    {"restrained", "cast", "", false, "", "", "restrained", ""},
};
static const SpellDice kDiceFreezingSphere[] = {
    {10, 6, 0, "cold", ""},
};
static const SpellGrant kGrantFreezingSphere[] = {
    {"restrained", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantGeas[] = {
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantGrease[] = {
    {"prone", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantGreaterInvisibility[] = {
    {"invisible", "cast", "", false, "", "", "", ""},
};
static const SpellDice kDiceGuidingBolt[] = {
    {4, 6, 0, "radiant", ""},
};
static const SpellGrant kGrantHallow[] = {
    {"frightened", "cast", "", false, "", "", "", ""},
};
static const SpellDice kDiceHarm[] = {
    {14, 6, 0, "necrotic", ""},
};
static const SpellDice kDiceHeatMetal[] = {
    {2, 8, 0, "fire", ""},
};
static const SpellDice kDiceHellishRebuke[] = {
    {2, 10, 0, "fire", ""},
};
static const SpellGrant kGrantHideousLaughter[] = {
    {"incapacitated prone", "failure", "", true, "", "", "", ""},
};
static const SpellGrant kGrantHoldMonster[] = {
    {"paralyzed", "failure", "", true, "", "", "", ""},
};
static const SpellGrant kGrantHoldPerson[] = {
    {"paralyzed", "failure", "", true, "", "", "", ""},
};
static const SpellGrant kGrantHolyAura[] = {
    {"blinded", "failure", "targetEnd", false, "", "", "", ""},
};
static const SpellGrant kGrantHypnoticPattern[] = {
    {"charmed", "failure", "", false, "", "takesDamage", "", ""},
    {"incapacitated", "failure", "", false, "", "takesDamage", "", "charmed"},
};
static const SpellDice kDiceIceKnife[] = {
    {1, 10, 0, "piercing", ""},
    {2, 6, 0, "cold", "aside"},
};
static const SpellDice kDiceIceStorm[] = {
    {2, 10, 0, "bludgeoning", ""},
    {4, 6, 0, "cold", ""},
};
static const SpellGrant kGrantImprisonment[] = {
    {"restrained", "failure", "", false, "", "", "", ""},
    {"unconscious", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceIncendiaryCloud[] = {
    {10, 8, 0, "fire", ""},
};
static const SpellDice kDiceInflictWounds[] = {
    {2, 10, 0, "necrotic", ""},
};
static const SpellDice kDiceInsectPlague[] = {
    {4, 10, 0, "piercing", ""},
};
static const SpellGrant kGrantInvisibility[] = {
    {"invisible", "cast", "", false, "", "attackRoll dealsDamage anySpell", "", ""},
};
static const SpellGrant kGrantIrresistibleDance[] = {
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceLightningBolt[] = {
    {8, 6, 0, "lightning", ""},
};
static const SpellDice kDiceMagicMissile[] = {
    {1, 4, 1, "force", ""},
};
static const SpellGrant kGrantMassSuggestion[] = {
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceMeteorSwarm[] = {
    {20, 6, 0, "fire", ""},
    {20, 6, 0, "bludgeoning", ""},
};
static const SpellDice kDiceMindSpike[] = {
    {3, 8, 0, "psychic", ""},
};
static const SpellGrant kGrantMislead[] = {
    {"invisible", "cast", "", false, "", "", "", ""},
};
static const SpellGrant kGrantModifyMemory[] = {
    {"charmed", "failure", "", false, "", "takesDamage", "", ""},
    {"incapacitated", "failure", "", false, "", "takesDamage", "", "charmed"},
};
static const SpellDice kDiceMoonbeam[] = {
    {2, 10, 0, "radiant", ""},
};
static const SpellDice kDicePhantasmalForce[] = {
    {2, 8, 0, "psychic", ""},
};
static const SpellDice kDicePhantasmalKiller[] = {
    {4, 10, 0, "psychic", ""},
};
static const SpellDice kDicePoisonSpray[] = {
    {1, 12, 0, "poison", ""},
};
static const SpellDice kDicePowerWordKill[] = {
    {12, 12, 0, "psychic", ""},
};
static const SpellGrant kGrantPowerWordStun[] = {
    {"stunned", "cast", "", true, "", "", "", ""},
};
static const SpellDice kDiceProduceFlame[] = {
    {1, 8, 0, "fire", ""},
};
static const SpellDice kDiceRayOfFrost[] = {
    {1, 8, 0, "cold", ""},
};
static const SpellDice kDiceRayOfSickness[] = {
    {2, 8, 0, "poison", ""},
};
static const SpellGrant kGrantRayOfSickness[] = {
    {"poisoned", "hit", "sourceEnd", false, "", "", "", ""},
};
static const SpellDice kDiceSacredFlame[] = {
    {1, 8, 0, "radiant", ""},
};
static const SpellDice kDiceScorchingRay[] = {
    {2, 6, 0, "fire", ""},
};
static const SpellDice kDiceSearingSmite[] = {
    {1, 6, 0, "fire", ""},
};
static const SpellGrant kGrantSeeInvisibility[] = {
    {"invisible", "cast", "", false, "", "", "", ""},
};
static const SpellGrant kGrantSequester[] = {
    {"invisible", "cast", "", false, "", "", "", ""},
    {"unconscious", "cast", "", false, "", "", "", ""},
};
static const SpellDice kDiceShatter[] = {
    {3, 8, 0, "thunder", ""},
};
static const SpellDice kDiceShockingGrasp[] = {
    {1, 8, 0, "lightning", ""},
};
static const SpellGrant kGrantSleep[] = {
    {"incapacitated", "failure", "", true, "unconscious", "takesDamage", "", ""},
};
static const SpellGrant kGrantSleetStorm[] = {
    {"prone", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceSorcerousBurst[] = {
    {1, 8, 0, "", ""},
};
static const SpellDice kDiceSpiritGuardians[] = {
    {3, 8, 0, "radiant", ""},
};
static const SpellDice kDiceSpiritualWeapon[] = {
    {1, 8, 0, "force", ""},
};
static const SpellDice kDiceStarryWisp[] = {
    {1, 8, 0, "radiant", ""},
};
static const SpellGrant kGrantStinkingCloud[] = {
    {"poisoned", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceStormOfVengeance[] = {
    {2, 6, 0, "thunder", ""},
    {4, 6, 0, "acid", "skip"},
    {1, 6, 0, "cold", "skip"},
};
static const SpellGrant kGrantStormOfVengeance[] = {
    {"deafened", "failure", "", false, "", "", "", ""},
};
static const SpellGrant kGrantSuggestion[] = {
    {"charmed", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceSunbeam[] = {
    {6, 8, 0, "radiant", ""},
};
static const SpellGrant kGrantSunbeam[] = {
    {"blinded", "failure", "sourceStart", false, "", "", "", ""},
};
static const SpellDice kDiceSunburst[] = {
    {12, 6, 0, "radiant", ""},
};
static const SpellGrant kGrantSunburst[] = {
    {"blinded", "failure", "", true, "", "", "", ""},
};
static const SpellGrant kGrantTelekinesis[] = {
    {"restrained", "failure", "sourceEnd", false, "", "", "", ""},
};
static const SpellDice kDiceThunderwave[] = {
    {2, 8, 0, "thunder", ""},
};
static const SpellDice kDiceTsunami[] = {
    {6, 10, 0, "bludgeoning", ""},
};
static const SpellDice kDiceVampiricTouch[] = {
    {3, 6, 0, "necrotic", ""},
};
static const SpellDice kDiceViciousMockery[] = {
    {1, 6, 0, "psychic", ""},
};
static const SpellDice kDiceVitriolicSphere[] = {
    {10, 4, 0, "acid", ""},
    {5, 4, 0, "acid", "later"},
};
static const SpellDice kDiceWallOfFire[] = {
    {5, 8, 0, "fire", ""},
    {5, 8, 0, "fire", "zone"},
};
static const SpellDice kDiceWallOfIce[] = {
    {10, 6, 0, "cold", ""},
    {5, 6, 0, "cold", "zone"},
};
static const SpellDice kDiceWallOfThorns[] = {
    {7, 8, 0, "piercing", ""},
    {7, 8, 0, "slashing", "zone"},
};
static const SpellGrant kGrantWeb[] = {
    {"restrained", "failure", "", false, "", "", "", ""},
};
static const SpellDice kDiceWeird[] = {
    {10, 10, 0, "psychic", ""},
    {5, 10, 0, "psychic", "repeat"},
};
static const SpellGrant kGrantWeird[] = {
    {"frightened", "failure", "", true, "", "", "", ""},
};
static const SpellGrant kGrantWindWalk[] = {
    {"stunned", "cast", "minute", false, "", "", "", ""},
};
static const SpellDice kDiceWindWall[] = {
    {4, 8, 0, "bludgeoning", ""},
};

static const SpellCombatRule kSpellRules[] = {
    {"acid-arrow", "Acid Arrow", 2, false, "attack", false, "", false, true, false, false, false, 1, 0, "", "", 0, "", "", 1, 4, "all", "", kDiceAcidArrow, 2, nullptr, 0, ""},
    {"acid-splash", "Acid Splash", 0, false, "save", false, "dexterity", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceAcidSplash, 1, nullptr, 0, ""},
    {"animal-friendship", "Animal Friendship", 1, false, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantAnimalFriendship, 1, ""},
    {"antipathy-sympathy", "Antipathy/Sympathy", 8, false, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantAntipathySympathy, 2, ""},
    {"arcane-hand", "Arcane Hand", 5, true, "attack", true, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 2, 8, "all", "", kDiceArcaneHand, 1, kGrantArcaneHand, 1, ""},
    {"arcane-sword", "Arcane Sword", 7, true, "attack", true, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceArcaneSword, 1, nullptr, 0, "Add the spellcasting ability modifier to the damage."},
    {"astral-projection", "Astral Projection", 9, false, "auto", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantAstralProjection, 1, ""},
    {"awaken", "Awaken", 5, false, "auto", false, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantAwaken, 1, ""},
    {"banishment", "Banishment", 4, true, "save", false, "charisma", false, false, false, false, false, 1, 1, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantBanishment, 1, ""},
    {"befuddlement", "Befuddlement", 8, false, "save", false, "intelligence", true, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceBefuddlement, 1, nullptr, 0, ""},
    {"bestow-curse", "Bestow Curse", 3, true, "save", false, "wisdom", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceBestowCurse, 1, nullptr, 0, ""},
    {"black-tentacles", "Black Tentacles", 4, true, "save", false, "strength", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceBlackTentacles, 1, kGrantBlackTentacles, 1, ""},
    {"blade-barrier", "Blade Barrier", 6, true, "save", false, "dexterity", true, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceBladeBarrier, 1, nullptr, 0, ""},
    {"blight", "Blight", 4, false, "save", false, "constitution", true, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceBlight, 1, nullptr, 0, ""},
    {"blindness-deafness", "Blindness/Deafness", 2, false, "save", false, "constitution", false, false, false, false, false, 1, 1, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantBlindnessDeafness, 2, ""},
    {"burning-hands", "Burning Hands", 1, false, "save", false, "dexterity", true, false, true, true, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceBurningHands, 1, nullptr, 0, ""},
    {"call-lightning", "Call Lightning", 3, true, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 10, "all", "", kDiceCallLightning, 1, nullptr, 0, ""},
    {"chain-lightning", "Chain Lightning", 6, false, "save", false, "dexterity", true, false, false, false, false, 4, 1, "", "", 0, "", "", 0, 0, "all", "", kDiceChainLightning, 1, nullptr, 0, ""},
    {"charm-monster", "Charm Monster", 4, false, "save", false, "wisdom", false, false, false, false, false, 1, 1, "you or your allies are fighting it", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantCharmMonster, 1, ""},
    {"charm-person", "Charm Person", 1, false, "save", false, "wisdom", false, false, false, false, false, 1, 1, "you or your allies are fighting it", "humanoid", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantCharmPerson, 1, ""},
    {"chill-touch", "Chill Touch", 0, false, "attack", true, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceChillTouch, 1, nullptr, 0, ""},
    {"chromatic-orb", "Chromatic Orb", 1, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceChromaticOrb, 1, nullptr, 0, "The damage type is chosen when the spell is cast."},
    {"circle-of-death", "Circle of Death", 6, false, "save", false, "constitution", true, false, true, false, false, 1, 0, "", "", 0, "", "", 2, 8, "all", "", kDiceCircleOfDeath, 1, nullptr, 0, ""},
    {"cloudkill", "Cloudkill", 5, true, "save", false, "constitution", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceCloudkill, 1, nullptr, 0, ""},
    {"color-spray", "Color Spray", 1, false, "save", false, "constitution", false, false, true, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantColorSpray, 1, ""},
    {"command", "Command", 1, false, "save", false, "wisdom", false, false, false, false, false, 1, 1, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantCommand, 1, ""},
    {"compulsion", "Compulsion", 4, true, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantCompulsion, 1, ""},
    {"cone-of-cold", "Cone of Cold", 5, false, "save", false, "constitution", true, false, true, true, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceConeOfCold, 1, nullptr, 0, ""},
    {"confusion", "Confusion", 4, true, "save", false, "wisdom", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, nullptr, 0, "On a failure it can't take Bonus Actions or Reactions and rolls 1d10 for its behavior. It repeats the save at the end of each of its turns."},
    {"conjure-animals", "Conjure Animals", 3, true, "save", false, "strength", false, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 10, "all", "", kDiceConjureAnimals, 1, nullptr, 0, ""},
    {"conjure-celestial", "Conjure Celestial", 7, true, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 12, "all", "", kDiceConjureCelestial, 1, nullptr, 0, ""},
    {"conjure-elemental", "Conjure Elemental", 5, true, "save", false, "dexterity", false, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceConjureElemental, 2, kGrantConjureElemental, 1, "The damage type is chosen when the spell is cast."},
    {"conjure-fey", "Conjure Fey", 6, true, "attack", true, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 12, "all", "", kDiceConjureFey, 1, kGrantConjureFey, 1, "Add the spellcasting ability modifier to the damage."},
    {"conjure-woodland-beings", "Conjure Woodland Beings", 4, true, "save", false, "wisdom", true, false, false, true, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceConjureWoodlandBeings, 1, nullptr, 0, ""},
    {"contact-other-plane", "Contact Other Plane", 5, false, "save", false, "intelligence", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceContactOtherPlane, 1, kGrantContactOtherPlane, 1, ""},
    {"contagion", "Contagion", 5, false, "save", false, "constitution", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceContagion, 1, kGrantContagion, 1, ""},
    {"control-water", "Control Water", 4, true, "save", false, "strength", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceControlWater, 1, nullptr, 0, ""},
    {"disintegrate", "Disintegrate", 6, false, "save", false, "dexterity", false, false, false, false, false, 1, 0, "", "", 0, "", "", 3, 6, "all", "", kDiceDisintegrate, 1, nullptr, 0, ""},
    {"dissonant-whispers", "Dissonant Whispers", 1, false, "save", false, "wisdom", true, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceDissonantWhispers, 1, nullptr, 0, ""},
    {"divine-word", "Divine Word", 7, false, "save", false, "charisma", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, nullptr, 0, "On a failure, apply the spell's Hit Point table."},
    {"dominate-beast", "Dominate Beast", 4, true, "save", false, "wisdom", false, false, false, false, false, 1, 0, "you or your allies are fighting it", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantDominateBeast, 1, ""},
    {"dominate-monster", "Dominate Monster", 8, true, "save", false, "wisdom", false, false, false, false, false, 1, 0, "you or your allies are fighting it", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantDominateMonster, 1, ""},
    {"dominate-person", "Dominate Person", 5, true, "save", false, "wisdom", false, false, false, false, false, 1, 0, "you or your allies are fighting it", "humanoid", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantDominatePerson, 1, ""},
    {"dragons-breath", "Dragon's Breath", 2, true, "save", false, "dexterity", true, false, true, true, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceDragonsBreath, 1, nullptr, 0, "The damage type is chosen when the spell is cast."},
    {"dream", "Dream", 5, false, "save", false, "wisdom", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceDream, 1, nullptr, 0, ""},
    {"earthquake", "Earthquake", 8, true, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceEarthquake, 1, kGrantEarthquake, 1, ""},
    {"eldritch-blast", "Eldritch Blast", 0, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceEldritchBlast, 1, nullptr, 0, ""},
    {"ensnaring-strike", "Ensnaring Strike", 1, true, "save", false, "strength", false, false, false, true, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceEnsnaringStrike, 1, kGrantEnsnaringStrike, 1, ""},
    {"entangle", "Entangle", 1, true, "save", false, "strength", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantEntangle, 1, ""},
    {"eyebite", "Eyebite", 6, true, "save", false, "wisdom", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantEyebite, 2, ""},
    {"faerie-fire", "Faerie Fire", 1, true, "save", false, "dexterity", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, nullptr, 0, "On a failure, attack rolls against it have Advantage, and it can't benefit from Invisible."},
    {"faithful-hound", "Faithful Hound", 4, false, "save", false, "dexterity", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceFaithfulHound, 1, nullptr, 0, ""},
    {"fear", "Fear", 3, true, "save", false, "wisdom", false, false, true, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantFear, 1, ""},
    {"finger-of-death", "Finger of Death", 7, false, "save", false, "constitution", true, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceFingerOfDeath, 1, nullptr, 0, ""},
    {"fire-bolt", "Fire Bolt", 0, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceFireBolt, 1, nullptr, 0, ""},
    {"fire-storm", "Fire Storm", 7, false, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceFireStorm, 1, nullptr, 0, ""},
    {"fireball", "Fireball", 3, false, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceFireball, 1, nullptr, 0, ""},
    {"flame-blade", "Flame Blade", 2, true, "attack", true, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceFlameBlade, 1, nullptr, 0, "Add the spellcasting ability modifier to the damage."},
    {"flame-strike", "Flame Strike", 5, false, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 6, "types", "fire radiant", kDiceFlameStrike, 2, nullptr, 0, ""},
    {"flaming-sphere", "Flaming Sphere", 2, true, "save", false, "dexterity", true, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceFlamingSphere, 2, nullptr, 0, ""},
    {"flesh-to-stone", "Flesh to Stone", 6, true, "save", false, "constitution", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantFleshToStone, 2, ""},
    {"freedom-of-movement", "Freedom of Movement", 4, false, "auto", false, "", false, false, false, true, false, 1, 1, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantFreedomOfMovement, 2, ""},
    {"freezing-sphere", "Freezing Sphere", 6, false, "save", false, "constitution", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceFreezingSphere, 1, kGrantFreezingSphere, 1, ""},
    {"geas", "Geas", 5, false, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantGeas, 1, ""},
    {"grease", "Grease", 1, false, "save", false, "dexterity", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantGrease, 1, ""},
    {"greater-invisibility", "Greater Invisibility", 4, true, "auto", false, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantGreaterInvisibility, 1, ""},
    {"guiding-bolt", "Guiding Bolt", 1, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceGuidingBolt, 1, nullptr, 0, ""},
    {"hallow", "Hallow", 5, false, "auto", false, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantHallow, 1, ""},
    {"harm", "Harm", 6, false, "save", false, "constitution", true, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceHarm, 1, nullptr, 0, ""},
    {"heat-metal", "Heat Metal", 2, true, "save", false, "constitution", false, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceHeatMetal, 1, nullptr, 0, ""},
    {"hellish-rebuke", "Hellish Rebuke", 1, false, "save", false, "dexterity", true, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 10, "all", "", kDiceHellishRebuke, 1, nullptr, 0, ""},
    {"hideous-laughter", "Hideous Laughter", 1, true, "save", false, "wisdom", false, false, false, false, false, 1, 1, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantHideousLaughter, 1, ""},
    {"hold-monster", "Hold Monster", 5, true, "save", false, "wisdom", false, false, false, false, false, 1, 1, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantHoldMonster, 1, ""},
    {"hold-person", "Hold Person", 2, true, "save", false, "wisdom", false, false, false, false, false, 1, 1, "", "humanoid", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantHoldPerson, 1, ""},
    {"holy-aura", "Holy Aura", 8, true, "save", false, "constitution", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantHolyAura, 1, ""},
    {"hypnotic-pattern", "Hypnotic Pattern", 3, true, "save", false, "wisdom", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantHypnoticPattern, 2, ""},
    {"ice-knife", "Ice Knife", 1, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 6, "types", "cold", kDiceIceKnife, 2, nullptr, 0, ""},
    {"ice-storm", "Ice Storm", 4, false, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 10, "types", "bludgeoning", kDiceIceStorm, 2, nullptr, 0, ""},
    {"imprisonment", "Imprisonment", 9, false, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantImprisonment, 2, ""},
    {"incendiary-cloud", "Incendiary Cloud", 8, true, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceIncendiaryCloud, 1, nullptr, 0, ""},
    {"inflict-wounds", "Inflict Wounds", 1, false, "save", false, "constitution", true, false, false, true, false, 1, 0, "", "", 0, "", "", 1, 10, "all", "", kDiceInflictWounds, 1, nullptr, 0, ""},
    {"insect-plague", "Insect Plague", 5, true, "save", false, "constitution", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 10, "all", "", kDiceInsectPlague, 1, nullptr, 0, ""},
    {"invisibility", "Invisibility", 2, true, "auto", false, "", false, false, false, true, false, 1, 1, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantInvisibility, 1, ""},
    {"irresistible-dance", "Irresistible Dance", 6, true, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantIrresistibleDance, 1, ""},
    {"lightning-bolt", "Lightning Bolt", 3, false, "save", false, "dexterity", true, false, true, true, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceLightningBolt, 1, nullptr, 0, ""},
    {"magic-missile", "Magic Missile", 1, false, "auto", false, "", false, false, false, false, true, 3, 1, "", "", 0, "", "", 0, 0, "all", "", kDiceMagicMissile, 1, nullptr, 0, ""},
    {"mass-suggestion", "Mass Suggestion", 6, false, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantMassSuggestion, 1, ""},
    {"meteor-swarm", "Meteor Swarm", 9, false, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceMeteorSwarm, 2, nullptr, 0, ""},
    {"mind-spike", "Mind Spike", 2, true, "save", false, "wisdom", true, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceMindSpike, 1, nullptr, 0, ""},
    {"mislead", "Mislead", 5, true, "auto", false, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantMislead, 1, ""},
    {"modify-memory", "Modify Memory", 5, true, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantModifyMemory, 2, ""},
    {"moonbeam", "Moonbeam", 2, true, "save", false, "constitution", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 10, "all", "", kDiceMoonbeam, 1, nullptr, 0, ""},
    {"phantasmal-force", "Phantasmal Force", 2, true, "save", false, "intelligence", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDicePhantasmalForce, 1, nullptr, 0, ""},
    {"phantasmal-killer", "Phantasmal Killer", 4, true, "save", false, "wisdom", true, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 10, "all", "", kDicePhantasmalKiller, 1, nullptr, 0, ""},
    {"poison-spray", "Poison Spray", 0, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDicePoisonSpray, 1, nullptr, 0, ""},
    {"power-word-kill", "Power Word Kill", 9, false, "auto", false, "", false, false, false, false, false, 1, 0, "", "", 100, "dies", "", 0, 0, "all", "", kDicePowerWordKill, 1, nullptr, 0, ""},
    {"power-word-stun", "Power Word Stun", 8, false, "auto", false, "", false, false, false, false, false, 1, 0, "", "", 150, "stunned", "constitution", 0, 0, "all", "", nullptr, 0, kGrantPowerWordStun, 1, ""},
    {"prismatic-spray", "Prismatic Spray", 7, false, "save", false, "dexterity", false, false, true, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, nullptr, 0, "Roll on the spell's table for each creature; the save is against that result."},
    {"prismatic-wall", "Prismatic Wall", 9, false, "save", false, "constitution", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, nullptr, 0, "Roll on the spell's table for each creature; the save is against that result."},
    {"produce-flame", "Produce Flame", 0, false, "attack", false, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceProduceFlame, 1, nullptr, 0, ""},
    {"ray-of-frost", "Ray of Frost", 0, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceRayOfFrost, 1, nullptr, 0, ""},
    {"ray-of-sickness", "Ray of Sickness", 1, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceRayOfSickness, 1, kGrantRayOfSickness, 1, ""},
    {"sacred-flame", "Sacred Flame", 0, false, "save", false, "dexterity", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceSacredFlame, 1, nullptr, 0, ""},
    {"scorching-ray", "Scorching Ray", 2, false, "attack", false, "", false, false, false, false, true, 3, 1, "", "", 0, "", "", 0, 0, "all", "", kDiceScorchingRay, 1, nullptr, 0, ""},
    {"searing-smite", "Searing Smite", 1, false, "save", false, "constitution", false, false, false, true, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceSearingSmite, 1, nullptr, 0, ""},
    {"see-invisibility", "See Invisibility", 2, false, "auto", false, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantSeeInvisibility, 1, ""},
    {"sequester", "Sequester", 7, false, "auto", false, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantSequester, 2, ""},
    {"shatter", "Shatter", 2, false, "save", false, "constitution", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceShatter, 1, nullptr, 0, ""},
    {"shocking-grasp", "Shocking Grasp", 0, false, "attack", true, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceShockingGrasp, 1, nullptr, 0, ""},
    {"sleep", "Sleep", 1, true, "save", false, "wisdom", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantSleep, 1, ""},
    {"sleet-storm", "Sleet Storm", 3, true, "save", false, "dexterity", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantSleetStorm, 1, ""},
    {"sorcerous-burst", "Sorcerous Burst", 0, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceSorcerousBurst, 1, nullptr, 0, "The damage type is chosen when the spell is cast."},
    {"spirit-guardians", "Spirit Guardians", 3, true, "save", false, "wisdom", true, false, true, true, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceSpiritGuardians, 1, nullptr, 0, "Radiant or Necrotic, depending on the caster."},
    {"spiritual-weapon", "Spiritual Weapon", 2, true, "attack", true, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceSpiritualWeapon, 1, nullptr, 0, "Add the spellcasting ability modifier to the damage."},
    {"starry-wisp", "Starry Wisp", 0, false, "attack", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceStarryWisp, 1, nullptr, 0, ""},
    {"stinking-cloud", "Stinking Cloud", 3, true, "save", false, "constitution", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantStinkingCloud, 1, ""},
    {"storm-of-vengeance", "Storm of Vengeance", 9, true, "save", false, "constitution", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceStormOfVengeance, 3, kGrantStormOfVengeance, 1, "Later turns deal Acid, then Lightning, then Bludgeoning hail, then Cold."},
    {"suggestion", "Suggestion", 2, true, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantSuggestion, 1, ""},
    {"sunbeam", "Sunbeam", 6, true, "save", false, "constitution", true, false, true, true, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceSunbeam, 1, kGrantSunbeam, 1, ""},
    {"sunburst", "Sunburst", 8, false, "save", false, "constitution", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceSunburst, 1, kGrantSunburst, 1, ""},
    {"telekinesis", "Telekinesis", 5, true, "save", false, "strength", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantTelekinesis, 1, ""},
    {"thunderwave", "Thunderwave", 1, false, "save", false, "constitution", true, false, true, true, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceThunderwave, 1, nullptr, 0, ""},
    {"tsunami", "Tsunami", 8, true, "save", false, "strength", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceTsunami, 1, nullptr, 0, ""},
    {"vampiric-touch", "Vampiric Touch", 3, true, "attack", true, "", false, false, false, true, false, 1, 0, "", "", 0, "", "", 1, 6, "all", "", kDiceVampiricTouch, 1, nullptr, 0, ""},
    {"vicious-mockery", "Vicious Mockery", 0, false, "save", false, "wisdom", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceViciousMockery, 1, nullptr, 0, ""},
    {"vitriolic-sphere", "Vitriolic Sphere", 4, false, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 2, 4, "initial", "", kDiceVitriolicSphere, 2, nullptr, 0, ""},
    {"wall-of-fire", "Wall of Fire", 4, true, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceWallOfFire, 2, nullptr, 0, ""},
    {"wall-of-ice", "Wall of Ice", 6, true, "save", false, "dexterity", true, false, false, false, false, 1, 0, "", "", 0, "", "", 2, 6, "all", "", kDiceWallOfIce, 2, nullptr, 0, ""},
    {"wall-of-thorns", "Wall of Thorns", 6, true, "save", false, "dexterity", true, false, true, false, false, 1, 0, "", "", 0, "", "", 1, 8, "all", "", kDiceWallOfThorns, 2, nullptr, 0, ""},
    {"web", "Web", 2, true, "save", false, "dexterity", false, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantWeb, 1, ""},
    {"weird", "Weird", 9, true, "save", false, "wisdom", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceWeird, 2, kGrantWeird, 1, ""},
    {"wind-walk", "Wind Walk", 6, false, "auto", false, "", false, false, false, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", nullptr, 0, kGrantWindWalk, 1, ""},
    {"wind-wall", "Wind Wall", 3, true, "save", false, "strength", true, false, true, false, false, 1, 0, "", "", 0, "", "", 0, 0, "all", "", kDiceWindWall, 1, nullptr, 0, ""},
};

