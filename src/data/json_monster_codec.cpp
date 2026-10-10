#include "data/json_monster_codec.h"

#include "core/text.h"

#include <optional>
#include <utility>

namespace combat::json_codec {

namespace {

using json_util::json;
using Error = MonsterDataError;

// "selfEffect": {"condition": "invisible", "source": "Vanish",
//                "concentration": "Vanish", "endsOn": ["attackRoll"]}
std::optional<SelfEffect> readSelfEffect(const json& value, const std::string& context)
{
    const auto it = value.find("selfEffect");
    if (it == value.end() || it->is_null()) {
        return std::nullopt;
    }
    const std::string effectContext = context + " selfEffect";
    json_util::requireObject<Error>(*it, effectContext);
    SelfEffect effect;
    effect.condition = asciiLower(json_util::readString<Error>(*it, "condition", effectContext));
    if (isBlank(effect.condition)) {
        throw Error(effectContext + ": condition is required.");
    }
    effect.source = json_util::readOptionalString<Error>(*it, "source", effectContext);
    effect.concentration = json_util::readOptionalString<Error>(*it, "concentration", effectContext);
    effect.endsOn = json_util::readStringList<Error>(*it, "endsOn", effectContext);
    return effect;
}

void writeSelfEffect(json& row, const std::optional<SelfEffect>& effect)
{
    if (!effect.has_value()) {
        return;
    }
    json value{{"condition", effect->condition}, {"endsOn", effect->endsOn}};
    if (!effect->source.empty()) {
        value["source"] = effect->source;
    }
    if (!effect->concentration.empty()) {
        value["concentration"] = effect->concentration;
    }
    row["selfEffect"] = std::move(value);
}

// "aura": {"ability": "wisdom", "dc": 11, "range": "...", "who": "...",
//          "condition": "frightened", "immuneOnSuccess": true, "whileActive": false,
//          "failureDie": 8, "failureTable": [{"upTo": 4, "text": "..."}],
//          "suppressedBy": ["Illusory Appearance"]}
std::optional<AuraSave> readAura(const json& value, const std::string& context)
{
    const auto it = value.find("aura");
    if (it == value.end() || it->is_null()) {
        return std::nullopt;
    }
    const std::string auraContext = context + " aura";
    json_util::requireObject<Error>(*it, auraContext);
    AuraSave aura;
    const std::string abilityName = json_util::readString<Error>(*it, "ability", auraContext);
    const auto ability = abilityFromKey(abilityName);
    if (!ability.has_value()) {
        throw Error(auraContext + ": unknown ability \"" + abilityName + "\".");
    }
    aura.ability = *ability;
    aura.dc = json_util::readInt<Error>(*it, "dc", auraContext);
    aura.range = json_util::readOptionalString<Error>(*it, "range", auraContext);
    aura.who = json_util::readOptionalString<Error>(*it, "who", auraContext);
    aura.condition = asciiLower(json_util::readOptionalString<Error>(*it, "condition", auraContext));
    aura.immuneOnSuccess = json_util::readBoolOr<Error>(*it, "immuneOnSuccess", false, auraContext);
    aura.whileActive = json_util::readBoolOr<Error>(*it, "whileActive", false, auraContext);
    aura.enemiesOnly = json_util::readBoolOr<Error>(*it, "enemiesOnly", false, auraContext);
    aura.creatureTypes = json_util::readStringList<Error>(*it, "creatureTypes", auraContext);
    aura.failureDie = json_util::readIntOr<Error>(*it, "failureDie", 0, auraContext);
    const auto table = it->find("failureTable");
    if (table != it->end() && !table->is_null()) {
        if (!table->is_array()) {
            throw Error(auraContext + ": field \"failureTable\" must be an array.");
        }
        for (const json& row : *table) {
            json_util::requireObject<Error>(row, auraContext + " failureTable");
            aura.failureTable.push_back(AuraOutcome{json_util::readInt<Error>(row, "upTo", auraContext),
                                                    json_util::readString<Error>(row, "text", auraContext)});
        }
    }
    aura.suppressedBy = json_util::readStringList<Error>(*it, "suppressedBy", auraContext);
    return aura;
}

void writeAura(json& row, const std::optional<AuraSave>& aura)
{
    if (!aura.has_value()) {
        return;
    }
    json value{{"ability", abilityKey(aura->ability)}, {"dc", aura->dc}};
    if (!aura->range.empty()) {
        value["range"] = aura->range;
    }
    if (!aura->who.empty()) {
        value["who"] = aura->who;
    }
    if (!aura->condition.empty()) {
        value["condition"] = aura->condition;
    }
    if (aura->immuneOnSuccess) {
        value["immuneOnSuccess"] = true;
    }
    if (aura->whileActive) {
        value["whileActive"] = true;
    }
    if (aura->enemiesOnly) {
        value["enemiesOnly"] = true;
    }
    if (!aura->creatureTypes.empty()) {
        value["creatureTypes"] = aura->creatureTypes;
    }
    if (aura->failureDie > 0) {
        value["failureDie"] = aura->failureDie;
        json table = json::array();
        for (const AuraOutcome& outcome : aura->failureTable) {
            table.push_back(json{{"upTo", outcome.upTo}, {"text", outcome.text}});
        }
        value["failureTable"] = std::move(table);
    }
    if (!aura->suppressedBy.empty()) {
        value["suppressedBy"] = aura->suppressedBy;
    }
    row["aura"] = std::move(value);
}

std::vector<DamagePart> readDamage(const json& value, const std::string& context, const char* key = "damage")
{
    std::vector<DamagePart> parts;
    const auto it = value.find(key);
    if (it == value.end()) {
        return parts;
    }
    if (!it->is_array()) {
        throw Error(context + ": field \"" + key + "\" must be an array.");
    }
    for (std::size_t i = 0; i < it->size(); ++i) {
        const std::string partContext = context + " " + key + " " + std::to_string(i + 1);
        const json& row = (*it)[i];
        json_util::requireObject<Error>(row, partContext);
        DamagePart part;
        part.dice = json_util::readString<Error>(row, "dice", partContext);
        part.type = asciiLower(json_util::readOptionalString<Error>(row, "type", partContext));
        const std::string when = json_util::readOptionalString<Error>(row, "when", partContext);
        if (!when.empty()) {
            const auto parsed = damageWhenFromKey(when);
            if (!parsed.has_value()) {
                throw Error(partContext + ": unknown \"when\" value \"" + when + "\".");
            }
            part.when = *parsed;
        }
        part.condition = json_util::readOptionalString<Error>(row, "if", partContext);
        if (!parseDice(part.dice).has_value()) {
            throw Error(partContext + ": \"" + part.dice + "\" is not a dice expression.");
        }
        parts.push_back(std::move(part));
    }
    return parts;
}

json damageJson(const std::vector<DamagePart>& parts, bool withWhen = true)
{
    json list = json::array();
    for (const DamagePart& part : parts) {
        json row{{"dice", part.dice}, {"type", part.type}};
        if (withWhen) {
            row["when"] = damageWhenKey(part.when);
        }
        if (!part.condition.empty()) {
            row["if"] = part.condition;
        }
        list.push_back(std::move(row));
    }
    return list;
}

std::optional<SaveSpec> readSave(const json& value, const char* key, const std::string& context)
{
    const auto save = value.find(key);
    if (save == value.end() || save->is_null()) {
        return std::nullopt;
    }
    const std::string saveContext = context + " " + key;
    json_util::requireObject<Error>(*save, saveContext);
    const std::string abilityName = json_util::readString<Error>(*save, "ability", saveContext);
    const auto ability = abilityFromKey(abilityName);
    if (!ability.has_value()) {
        throw Error(saveContext + ": unknown ability \"" + abilityName + "\".");
    }
    SaveSpec spec;
    spec.ability = *ability;
    spec.dc = json_util::readInt<Error>(*save, "dc", saveContext);
    spec.halfOnSuccess = json_util::readOptionalString<Error>(*save, "onSuccess", saveContext) == "half";
    spec.advantageIf = json_util::readOptionalString<Error>(*save, "advantageIf", saveContext);
    return spec;
}

json saveJson(const SaveSpec& save)
{
    json row{{"ability", abilityKey(save.ability)}, {"dc", save.dc}, {"onSuccess", save.halfOnSuccess ? "half" : "none"}};
    if (!save.advantageIf.empty()) {
        row["advantageIf"] = save.advantageIf;
    }
    return row;
}

std::vector<std::string> lowerList(std::vector<std::string> list)
{
    for (std::string& item : list) {
        item = asciiLower(trim(item));
    }
    return list;
}

// "riders": [{"conditions": ["grappled"], "on": "hit", "targetMaxSize": "Large",
//             "escapeDc": 14}, ...]
std::vector<ConditionRider> readRiders(const json& value, const std::string& context)
{
    std::vector<ConditionRider> riders;
    const auto it = value.find("riders");
    if (it == value.end() || it->is_null()) {
        return riders;
    }
    if (!it->is_array()) {
        throw Error(context + ": field \"riders\" must be an array.");
    }
    for (std::size_t i = 0; i < it->size(); ++i) {
        const std::string riderContext = context + " rider " + std::to_string(i + 1);
        const json& row = (*it)[i];
        json_util::requireObject<Error>(row, riderContext);
        ConditionRider rider;
        rider.conditions = lowerList(json_util::readStringList<Error>(row, "conditions", riderContext));
        rider.concentrationDisadvantage =
            json_util::readBoolOr<Error>(row, "concentrationDisadvantage", false, riderContext);
        if (rider.conditions.empty() && !rider.concentrationDisadvantage) {
            throw Error(riderContext + ": at least one condition is required.");
        }
        const std::string on = json_util::readOptionalString<Error>(row, "on", riderContext);
        rider.on = on.empty() ? std::string(kRiderOnHit) : on;
        rider.targetMaxSize = json_util::readOptionalString<Error>(row, "targetMaxSize", riderContext);
        rider.targetMaxHp = json_util::readOptionalInt<Error>(row, "targetMaxHp", riderContext);
        rider.exceptTypes = lowerList(json_util::readStringList<Error>(row, "exceptTypes", riderContext));
        rider.ask = json_util::readOptionalString<Error>(row, "ask", riderContext);
        rider.refundDamage = json_util::readBoolOr<Error>(row, "refundDamage", false, riderContext);
        rider.escapeDc = json_util::readOptionalInt<Error>(row, "escapeDc", riderContext);
        rider.until = json_util::readOptionalString<Error>(row, "until", riderContext);
        rider.tiedTo = asciiLower(json_util::readOptionalString<Error>(row, "tiedTo", riderContext));
        rider.saveEnds = json_util::readBoolOr<Error>(row, "saveEnds", false, riderContext);
        rider.saveOnDemand = json_util::readBoolOr<Error>(row, "saveOnDemand", false, riderContext);
        rider.requiresSenses = json_util::readBoolOr<Error>(row, "requiresSenses", false, riderContext);
        rider.saveAtStart = json_util::readBoolOr<Error>(row, "saveAtStart", false, riderContext);
        rider.saveFailDamage = readDamage(row, riderContext, "saveFailDamage");
        rider.endsOn = json_util::readStringList<Error>(row, "endsOn", riderContext);
        rider.worsensTo = lowerList(json_util::readStringList<Error>(row, "worsensTo", riderContext));
        rider.worseSaveEnds = json_util::readBoolOr<Error>(row, "worseSaveEnds", false, riderContext);
        rider.worseEndsOn = json_util::readStringList<Error>(row, "worseEndsOn", riderContext);
        rider.ongoing = readDamage(row, riderContext, "ongoing");
        rider.ongoingAt = json_util::readOptionalString<Error>(row, "ongoingAt", riderContext);
        rider.removes = lowerList(json_util::readStringList<Error>(row, "removes", riderContext));
        rider.stabilize = json_util::readBoolOr<Error>(row, "stabilize", false, riderContext);
        rider.concentration = json_util::readOptionalString<Error>(row, "concentration", riderContext);
        riders.push_back(std::move(rider));
    }
    return riders;
}

json ridersJson(const std::vector<ConditionRider>& riders)
{
    json list = json::array();
    for (const ConditionRider& rider : riders) {
        json row{{"conditions", rider.conditions}, {"on", rider.on}};
        if (!rider.targetMaxSize.empty()) {
            row["targetMaxSize"] = rider.targetMaxSize;
        }
        if (rider.targetMaxHp.has_value()) {
            row["targetMaxHp"] = *rider.targetMaxHp;
        }
        if (!rider.exceptTypes.empty()) {
            row["exceptTypes"] = rider.exceptTypes;
        }
        if (!rider.ask.empty()) {
            row["ask"] = rider.ask;
        }
        if (rider.refundDamage) {
            row["refundDamage"] = true;
        }
        if (rider.escapeDc.has_value()) {
            row["escapeDc"] = *rider.escapeDc;
        }
        if (!rider.until.empty()) {
            row["until"] = rider.until;
        }
        if (!rider.tiedTo.empty()) {
            row["tiedTo"] = rider.tiedTo;
        }
        if (rider.saveEnds) {
            row["saveEnds"] = true;
        }
        if (rider.saveOnDemand) {
            row["saveOnDemand"] = true;
        }
        if (rider.requiresSenses) {
            row["requiresSenses"] = true;
        }
        if (rider.saveAtStart) {
            row["saveAtStart"] = true;
        }
        if (!rider.saveFailDamage.empty()) {
            row["saveFailDamage"] = damageJson(rider.saveFailDamage, false);
        }
        if (!rider.endsOn.empty()) {
            row["endsOn"] = rider.endsOn;
        }
        if (!rider.worsensTo.empty()) {
            row["worsensTo"] = rider.worsensTo;
        }
        if (rider.worseSaveEnds) {
            row["worseSaveEnds"] = true;
        }
        if (!rider.worseEndsOn.empty()) {
            row["worseEndsOn"] = rider.worseEndsOn;
        }
        if (!rider.ongoing.empty()) {
            row["ongoing"] = damageJson(rider.ongoing, false);
        }
        if (!rider.ongoingAt.empty()) {
            row["ongoingAt"] = rider.ongoingAt;
        }
        if (!rider.removes.empty()) {
            row["removes"] = rider.removes;
        }
        if (rider.stabilize) {
            row["stabilize"] = true;
        }
        if (rider.concentrationDisadvantage) {
            row["concentrationDisadvantage"] = true;
        }
        if (!rider.concentration.empty()) {
            row["concentration"] = rider.concentration;
        }
        list.push_back(std::move(row));
    }
    return list;
}

// Everything on an action but its name, effect, count, and self effect. A
// targeted feature uses the same fields.
void readAttackFields(const json& value, MonsterAttack& attack, const std::string& attackContext)
{
    attack.attackBonus = json_util::readOptionalInt<Error>(value, "attackBonus", attackContext);
    attack.save = readSave(value, "save", attackContext);
    attack.damage = readDamage(value, attackContext);
    attack.recharge = json_util::readOptionalInt<Error>(value, "recharge", attackContext);
    attack.perDay = json_util::readOptionalInt<Error>(value, "perDay", attackContext);
    attack.area = json_util::readBoolOr<Error>(value, "area", false, attackContext);
    attack.inMultiattack = json_util::readBoolOr<Error>(value, "inMultiattack", false, attackContext);
    attack.targetCondition = asciiLower(json_util::readOptionalString<Error>(value, "targetCondition", attackContext));
    attack.targetMaxSize = json_util::readOptionalString<Error>(value, "targetMaxSize", attackContext);
    attack.targetTypes = lowerList(json_util::readStringList<Error>(value, "targetTypes", attackContext));
    attack.targetExceptTypes = lowerList(json_util::readStringList<Error>(value, "targetExceptTypes", attackContext));
    attack.targetAtZeroHp = json_util::readBoolOr<Error>(value, "targetAtZeroHp", false, attackContext);
    attack.failureHpThreshold = json_util::readOptionalInt<Error>(value, "failureHpThreshold", attackContext);
    attack.failureHpEffect = json_util::readOptionalString<Error>(value, "failureHpEffect", attackContext);
    attack.failureSelfHealing = json_util::readOptionalString<Error>(value, "failureSelfHealing", attackContext);
    attack.riders = readRiders(value, attackContext);
    attack.riderSave = readSave(value, "riderSave", attackContext);
    const auto drain = value.find("drain");
    if (drain != value.end() && !drain->is_null()) {
        const std::string drainContext = attackContext + " drain";
        json_util::requireObject<Error>(*drain, drainContext);
        HpDrain row;
        row.type = asciiLower(json_util::readOptionalString<Error>(*drain, "type", drainContext));
        row.heals = json_util::readBoolOr<Error>(*drain, "heals", false, drainContext);
        attack.drain = row;
    }
    attack.advantageIfGrappled = json_util::readBoolOr<Error>(value, "advantageIfGrappled", false, attackContext);
    if (const auto benefit = value.find("benefit"); benefit != value.end() && !benefit->is_null()) {
        const std::string benefitContext = attackContext + " benefit";
        json_util::requireObject<Error>(*benefit, benefitContext);
        Benefit row;
        row.tempHp = json_util::readOptionalString<Error>(*benefit, "tempHp", benefitContext);
        row.healing = json_util::readOptionalString<Error>(*benefit, "healing", benefitContext);
        for (const std::string* dice : {&row.tempHp, &row.healing}) {
            if (!dice->empty() && !parseDice(*dice).has_value()) {
                throw Error(benefitContext + ": \"" + *dice + "\" is not a dice expression.");
            }
        }
        row.advantageOnAttacks = json_util::readBoolOr<Error>(*benefit, "advantageOnAttacks", false, benefitContext);
        row.acBonus = json_util::readIntOr<Error>(*benefit, "acBonus", 0, benefitContext);
        row.until = json_util::readOptionalString<Error>(*benefit, "until", benefitContext);
        attack.benefit = row;
    }
    attack.multiattackAs = json_util::readOptionalString<Error>(value, "multiattackAs", attackContext);
    attack.halfDamageOnMiss = json_util::readBoolOr<Error>(value, "halfDamageOnMiss", false, attackContext);
    attack.laterDamage = readDamage(value, attackContext, "laterDamage");
    attack.allowSelf = json_util::readBoolOr<Error>(value, "allowSelf", false, attackContext);
    attack.selfOnly = json_util::readBoolOr<Error>(value, "selfOnly", false, attackContext);
    attack.strikes = json_util::readIntOr<Error>(value, "strikes", 1, attackContext);
    attack.repeatSameTarget = json_util::readBoolOr<Error>(value, "repeatSameTarget", false, attackContext);
    attack.concentration = json_util::readOptionalString<Error>(value, "concentration", attackContext);
    attack.repeatSave = readSave(value, "repeatSave", attackContext);
    attack.castOnly = json_util::readBoolOr<Error>(value, "castOnly", false, attackContext);
    attack.failureOutcome = json_util::readOptionalString<Error>(value, "failureOutcome", attackContext);
    attack.successOutcome = json_util::readOptionalString<Error>(value, "successOutcome", attackContext);
}

void writeAttackFields(json& row, const MonsterAttack& attack)
{
    if (attack.attackBonus.has_value()) {
        row["attackBonus"] = *attack.attackBonus;
    }
    if (attack.save.has_value()) {
        row["save"] = saveJson(*attack.save);
    }
    if (!attack.damage.empty()) {
        row["damage"] = damageJson(attack.damage);
    }
    if (attack.recharge.has_value()) {
        row["recharge"] = *attack.recharge;
    }
    if (attack.perDay.has_value()) {
        row["perDay"] = *attack.perDay;
    }
    if (attack.area) {
        row["area"] = true;
    }
    if (attack.inMultiattack) {
        row["inMultiattack"] = true;
    }
    if (!attack.targetCondition.empty()) {
        row["targetCondition"] = attack.targetCondition;
    }
    if (!attack.targetMaxSize.empty()) {
        row["targetMaxSize"] = attack.targetMaxSize;
    }
    if (!attack.targetTypes.empty()) {
        row["targetTypes"] = attack.targetTypes;
    }
    if (!attack.targetExceptTypes.empty()) {
        row["targetExceptTypes"] = attack.targetExceptTypes;
    }
    if (attack.targetAtZeroHp) {
        row["targetAtZeroHp"] = true;
    }
    if (attack.failureHpThreshold.has_value()) {
        row["failureHpThreshold"] = *attack.failureHpThreshold;
        row["failureHpEffect"] = attack.failureHpEffect;
    }
    if (!attack.failureSelfHealing.empty()) {
        row["failureSelfHealing"] = attack.failureSelfHealing;
    }
    if (!attack.riders.empty()) {
        row["riders"] = ridersJson(attack.riders);
    }
    if (attack.riderSave.has_value()) {
        row["riderSave"] = saveJson(*attack.riderSave);
    }
    if (attack.drain.has_value()) {
        json drain{{"type", attack.drain->type}};
        if (attack.drain->heals) {
            drain["heals"] = true;
        }
        row["drain"] = std::move(drain);
    }
    if (attack.advantageIfGrappled) {
        row["advantageIfGrappled"] = true;
    }
    if (attack.benefit.has_value()) {
        const Benefit& benefit = *attack.benefit;
        json value = json::object();
        if (!benefit.tempHp.empty()) {
            value["tempHp"] = benefit.tempHp;
        }
        if (!benefit.healing.empty()) {
            value["healing"] = benefit.healing;
        }
        if (benefit.advantageOnAttacks) {
            value["advantageOnAttacks"] = true;
        }
        if (benefit.acBonus != 0) {
            value["acBonus"] = benefit.acBonus;
        }
        if (!benefit.until.empty()) {
            value["until"] = benefit.until;
        }
        row["benefit"] = std::move(value);
    }
    if (!attack.multiattackAs.empty()) {
        row["multiattackAs"] = attack.multiattackAs;
    }
    if (attack.halfDamageOnMiss) {
        row["halfDamageOnMiss"] = true;
    }
    if (!attack.laterDamage.empty()) {
        row["laterDamage"] = damageJson(attack.laterDamage, false);
    }
    if (attack.allowSelf) {
        row["allowSelf"] = true;
    }
    if (attack.castOnly) {
        row["castOnly"] = true;
    }
    if (!attack.failureOutcome.empty()) {
        row["failureOutcome"] = attack.failureOutcome;
    }
    if (!attack.successOutcome.empty()) {
        row["successOutcome"] = attack.successOutcome;
    }
    if (attack.selfOnly) {
        row["selfOnly"] = true;
    }
    if (attack.strikes != 1) {
        row["strikes"] = attack.strikes;
    }
    if (attack.repeatSameTarget) {
        row["repeatSameTarget"] = true;
    }
    if (!attack.concentration.empty()) {
        row["concentration"] = attack.concentration;
    }
    if (attack.repeatSave.has_value()) {
        row["repeatSave"] = saveJson(*attack.repeatSave);
    }
}

std::vector<MonsterAttack> readAttacks(const json& object, const std::string& context)
{
    const auto it = object.find("attacks");
    if (it == object.end()) {
        return {};
    }
    if (!it->is_array()) {
        throw Error(context + ": field \"attacks\" must be an array.");
    }
    std::vector<MonsterAttack> attacks;
    attacks.reserve(it->size());
    for (std::size_t i = 0; i < it->size(); ++i) {
        const std::string attackContext = context + " attack " + std::to_string(i + 1);
        const json& value = (*it)[i];
        json_util::requireObject<Error>(value, attackContext);
        MonsterAttack attack;
        attack.name = json_util::readString<Error>(value, "name", attackContext);
        attack.effect = json_util::readString<Error>(value, "effect", attackContext);
        attack.count = json_util::readInt<Error>(value, "count", attackContext);
        if (isBlank(attack.name)) {
            throw Error(attackContext + ": name is required.");
        }
        if (attack.count < 1) {
            throw Error(attackContext + ": count must be at least 1.");
        }
        readAttackFields(value, attack, attackContext);
        attack.selfEffect = readSelfEffect(value, attackContext);
        attacks.push_back(std::move(attack));
    }
    return attacks;
}

std::vector<MonsterFeature> readFeatures(const json& object, const char* key, const std::string& context)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return {};
    }
    if (!it->is_array()) {
        throw Error(context + ": field \"" + key + "\" must be an array.");
    }
    std::vector<MonsterFeature> features;
    features.reserve(it->size());
    for (std::size_t i = 0; i < it->size(); ++i) {
        const std::string featureContext = context + " " + key + " " + std::to_string(i + 1);
        const json& value = (*it)[i];
        json_util::requireObject<Error>(value, featureContext);
        MonsterFeature feature;
        feature.name = json_util::readString<Error>(value, "name", featureContext);
        feature.effect = json_util::readString<Error>(value, "effect", featureContext);
        if (isBlank(feature.name)) {
            throw Error(featureContext + ": name is required.");
        }
        feature.selfEffect = readSelfEffect(value, featureContext);
        feature.recharge = json_util::readOptionalInt<Error>(value, "recharge", featureContext);
        feature.perDay = json_util::readOptionalInt<Error>(value, "perDay", featureContext);
        feature.aura = readAura(value, featureContext);
        if (const auto modifier = value.find("attackModifier"); modifier != value.end() && !modifier->is_null()) {
            const std::string modifierContext = featureContext + " attackModifier";
            json_util::requireObject<Error>(*modifier, modifierContext);
            AttackModifier row;
            row.advantage = json_util::readOptionalString<Error>(*modifier, "mode", modifierContext) != "disadvantage";
            row.when = json_util::readOptionalString<Error>(*modifier, "when", modifierContext);
            row.ask = json_util::readOptionalString<Error>(*modifier, "ask", modifierContext);
            row.sticky = json_util::readBoolOr<Error>(*modifier, "sticky", false, modifierContext);
            row.meleeOnly = json_util::readBoolOr<Error>(*modifier, "meleeOnly", false, modifierContext);
            row.alliesToo = json_util::readBoolOr<Error>(*modifier, "alliesToo", false, modifierContext);
            row.whileActive = json_util::readBoolOr<Error>(*modifier, "whileActive", false, modifierContext);
            row.damageType = asciiLower(json_util::readOptionalString<Error>(*modifier, "damageType", modifierContext));
            feature.attackModifier = row;
        }
        feature.afterDamagingBloodied =
            json_util::readOptionalString<Error>(value, "trigger", featureContext) == "damagedBloodied";
        if (value.contains("save") || value.contains("attackBonus") || value.contains("benefit")) {
            MonsterAttack targeted;
            targeted.name = feature.name;
            targeted.effect = feature.effect;
            readAttackFields(value, targeted, featureContext);
            // Its recharge and uses are the feature's own.
            targeted.recharge.reset();
            targeted.perDay.reset();
            feature.targeted = std::move(targeted);
        }
        features.push_back(std::move(feature));
    }
    return features;
}

std::vector<std::string> readTypes(const json& object, const char* key, const std::string& context)
{
    std::vector<std::string> types = json_util::readStringList<Error>(object, key, context);
    for (std::string& type : types) {
        type = asciiLower(trim(type));
    }
    return types;
}

void writeFeatures(json& document, const char* key, const std::vector<MonsterFeature>& features)
{
    if (features.empty()) {
        return;
    }
    json list = json::array();
    for (const MonsterFeature& feature : features) {
        json row{{"name", feature.name}, {"effect", feature.effect}};
        if (feature.recharge.has_value()) {
            row["recharge"] = *feature.recharge;
        }
        if (feature.perDay.has_value()) {
            row["perDay"] = *feature.perDay;
        }
        if (feature.targeted.has_value()) {
            MonsterAttack targeted = *feature.targeted;
            targeted.recharge.reset();
            targeted.perDay.reset();
            writeAttackFields(row, targeted);
        }
        writeSelfEffect(row, feature.selfEffect);
        writeAura(row, feature.aura);
        if (feature.afterDamagingBloodied) {
            row["trigger"] = "damagedBloodied";
        }
        if (feature.attackModifier.has_value()) {
            const AttackModifier& modifier = *feature.attackModifier;
            json value{{"mode", modifier.advantage ? "advantage" : "disadvantage"}};
            if (!modifier.when.empty()) {
                value["when"] = modifier.when;
            }
            if (!modifier.ask.empty()) {
                value["ask"] = modifier.ask;
            }
            if (modifier.sticky) {
                value["sticky"] = true;
            }
            if (modifier.meleeOnly) {
                value["meleeOnly"] = true;
            }
            if (modifier.alliesToo) {
                value["alliesToo"] = true;
            }
            if (modifier.whileActive) {
                value["whileActive"] = true;
            }
            if (!modifier.damageType.empty()) {
                value["damageType"] = modifier.damageType;
            }
            row["attackModifier"] = std::move(value);
        }
        list.push_back(std::move(row));
    }
    document[key] = std::move(list);
}

void writeList(json& document, const char* key, const std::vector<std::string>& list)
{
    if (!list.empty()) {
        document[key] = list;
    }
}

}  // namespace

Monster monsterFromJson(const json& value, const std::string& context)
{
    json_util::requireObject<Error>(value, context);
    Monster monster;
    monster.id = json_util::readString<Error>(value, "id", context);
    monster.name = json_util::readString<Error>(value, "name", context);
    monster.size = json_util::readString<Error>(value, "size", context);
    monster.creatureType = json_util::readString<Error>(value, "creatureType", context);
    monster.ac = json_util::readInt<Error>(value, "ac", context);
    monster.hp = json_util::readInt<Error>(value, "hp", context);
    monster.hitDice = json_util::readString<Error>(value, "hitDice", context);
    monster.speed = json_util::readString<Error>(value, "speed", context);
    monster.initiativeBonus = json_util::readInt<Error>(value, "initiativeBonus", context);
    monster.passivePerception = json_util::readInt<Error>(value, "passivePerception", context);
    monster.challengeRating = json_util::readString<Error>(value, "challengeRating", context);
    monster.source = json_util::readString<Error>(value, "source", context);

    const json& abilities = json_util::requireField<Error>(value, "abilities", context);
    if (!abilities.is_object()) {
        throw Error(context + ": field \"abilities\" must be an object.");
    }
    const std::string abilityContext = context + " abilities";
    for (const Ability ability : kAbilityOrder) {
        abilityScoreRef(monster.abilities, ability) = json_util::readInt<Error>(abilities, abilityKey(ability), abilityContext);
    }
    monster.attacks = readAttacks(value, context);
    monster.traits = readFeatures(value, "traits", context);
    monster.bonusActions = readFeatures(value, "bonusActions", context);
    monster.reactions = readFeatures(value, "reactions", context);
    monster.legendaryActions = readFeatures(value, "legendaryActions", context);
    monster.legendaryActionUses = json_util::readIntOr<Error>(value, "legendaryActionUses", 0, context);
    monster.defenses.resistances = readTypes(value, "damageResistances", context);
    monster.defenses.immunities = readTypes(value, "damageImmunities", context);
    monster.defenses.vulnerabilities = readTypes(value, "damageVulnerabilities", context);
    monster.conditionImmunities = readTypes(value, "conditionImmunities", context);
    const auto saves = value.find("savingThrows");
    if (saves != value.end() && !saves->is_null()) {
        const std::string saveContext = context + " savingThrows";
        json_util::requireObject<Error>(*saves, saveContext);
        for (const Ability ability : kAbilityOrder) {
            monster.savingThrows[static_cast<std::size_t>(ability)] =
                json_util::readOptionalInt<Error>(*saves, abilityKey(ability), saveContext);
        }
    }
    if (const auto skills = value.find("skills"); skills != value.end() && !skills->is_null()) {
        const std::string skillContext = context + " skills";
        json_util::requireObject<Error>(*skills, skillContext);
        for (auto it = skills->begin(); it != skills->end(); ++it) {
            monster.skills[it.key()] = json_util::readInt<Error>(*skills, it.key().c_str(), skillContext);
        }
    }
    monster.xp = json_util::readOptionalInt<Error>(value, "xp", context);
    return monster;
}

json monsterToJson(const Monster& monster)
{
    json abilities = json::object();
    for (const Ability ability : kAbilityOrder) {
        abilities[abilityKey(ability)] = abilityScore(monster.abilities, ability);
    }
    json document{
        {"id", monster.id},
        {"name", monster.name},
        {"size", monster.size},
        {"creatureType", monster.creatureType},
        {"ac", monster.ac},
        {"hp", monster.hp},
        {"hitDice", monster.hitDice},
        {"speed", monster.speed},
        {"initiativeBonus", monster.initiativeBonus},
        {"abilities", std::move(abilities)},
        {"passivePerception", monster.passivePerception},
        {"challengeRating", monster.challengeRating},
        {"source", monster.source},
    };
    if (!monster.attacks.empty()) {
        json attacks = json::array();
        for (const MonsterAttack& attack : monster.attacks) {
            json row{{"name", attack.name}, {"effect", attack.effect}, {"count", attack.count}};
            writeAttackFields(row, attack);
            writeSelfEffect(row, attack.selfEffect);
            attacks.push_back(std::move(row));
        }
        document["attacks"] = std::move(attacks);
    }
    writeFeatures(document, "traits", monster.traits);
    writeFeatures(document, "bonusActions", monster.bonusActions);
    writeFeatures(document, "reactions", monster.reactions);
    writeFeatures(document, "legendaryActions", monster.legendaryActions);
    if (monster.legendaryActionUses > 0) {
        document["legendaryActionUses"] = monster.legendaryActionUses;
    }
    writeList(document, "damageResistances", monster.defenses.resistances);
    writeList(document, "damageImmunities", monster.defenses.immunities);
    writeList(document, "damageVulnerabilities", monster.defenses.vulnerabilities);
    writeList(document, "conditionImmunities", monster.conditionImmunities);
    json saves = json::object();
    for (const Ability ability : kAbilityOrder) {
        const auto& bonus = monster.savingThrows[static_cast<std::size_t>(ability)];
        if (bonus.has_value()) {
            saves[abilityKey(ability)] = *bonus;
        }
    }
    if (!saves.empty()) {
        document["savingThrows"] = std::move(saves);
    }
    if (!monster.skills.empty()) {
        json skills = json::object();
        for (const auto& [skill, bonus] : monster.skills) {
            skills[skill] = bonus;
        }
        document["skills"] = std::move(skills);
    }
    if (monster.xp.has_value()) {
        document["xp"] = *monster.xp;
    }
    return document;
}

}  // namespace combat::json_codec
