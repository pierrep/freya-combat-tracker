#!/usr/bin/env python3
"""Adds structured combat data to data/srd/monsters.json (schemaVersion 2).

The stat-block prose in monsters.json is the SRD 5.2.1 text. This script runs
once, when the catalog is rebuilt, so the app never has to guess from prose at
runtime. It:

- strips headings from the next stat block that the original extraction left
  on the end of a monster's last entry ("... Slashing damage. Pegasus");
- adds, per action: attackBonus, save (ability, dc, onSuccess), damage parts
  (dice, type, when), recharge, perDay, area, and inMultiattack;
- corrects Multiattack counts the prose numbers differently;
- adds legendaryActionUses, and, from the 2024 data in a 5e-bits/5e-srd-api
  checkout (packages/5e-database, MIT, itself built from the SRD 5.2.1;
  https://github.com/5e-bits/5e-srd-api/tree/main/packages/5e-database/src/2024/en),
  damage resistances, immunities, vulnerabilities, condition immunities,
  saving-throw bonuses, skills, and XP. (The separate 5e-bits/5e-database
  repository is deprecated.)

Usage: build_srd_monsters.py <monsters.json> <5e-srd-api/packages/5e-database/src/2024/en/5e-SRD-Monsters.json>
The file is rewritten in place.
"""
import json
import re
import sys

DAMAGE_TYPES = ["acid", "bludgeoning", "cold", "fire", "force", "lightning", "necrotic", "piercing",
                "poison", "psychic", "radiant", "slashing", "thunder"]
ABILITIES = {"str": "strength", "dex": "dexterity", "con": "constitution", "int": "intelligence",
             "wis": "wisdom", "cha": "charisma"}
ABILITY_WORDS = {"Strength": "strength", "Dexterity": "dexterity", "Constitution": "constitution",
                 "Intelligence": "intelligence", "Wisdom": "wisdom", "Charisma": "charisma"}
NUMBER_WORDS = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7, "eight": 8}
DB_ALIASES = {"vampire": "vampire-vampire", "werebear": "werebear-human", "wereboar": "wereboar-human",
              "wererat": "wererat-human", "weretiger": "weretiger-human", "werewolf": "werewolf-human"}
# Multiattack totals whose prose does not number every part.
MULTIATTACK_OVERRIDES = {"behir": 2, "giant-constrictor-snake": 2, "hydra": 5, "mummy": 3, "mummy-lord": 2,
                         "dryad": 2}

DICE = r"(\d+)d(\d+)(?:\s*([+\-\u2212\u2013])\s*(\d+))?"
TYPED = r"(" + "|".join(t.capitalize() for t in DAMAGE_TYPES) + r")"
# "9 (2d4 + 4) Fire damage", or "28 (8d6) damage of the type chosen ..." (untyped).
ROLLED_RE = re.compile(r"\b\d+ \(" + DICE + r"\) (?:" + TYPED + r" )?damage")
FLAT_RE = re.compile(r"(?<![\d(])\b(\d+) " + TYPED + r" damage")


def strip_leak(text, names):
    match = re.search(r"\.\s+([A-Z][A-Za-z’' \-]{2,40})$", text.rstrip())
    if match and not re.search(r"[.:]", match.group(1)):
        return text[: match.start() + 1]
    return text


def dice_text(count, sides, sign, modifier):
    text = f"{count}d{sides}"
    if modifier:
        text += ("+" if sign == "+" else "-") + modifier
    return text


def classify(after, before):
    after = after.lower()
    before = before.lower()
    if before.rstrip().endswith(", or") or before.rstrip().endswith("\u2014or"):
        return "advantageAlt" if "advantage" in after else "alternative"
    if "if the attack roll had advantage" in after:
        return "advantage"
    if re.search(r"(start|end) of each of", after + " " + before):
        return "ongoing"
    clause = re.split(r"[,.\u2014]", after, maxsplit=1)[0]
    tail = re.split(r"[,\u2014]", before)[-1]
    if re.search(r"\bif\b", clause) or "extra" in before or tail.lstrip().startswith("if "):
        return "conditional"
    return "always"


def damage_condition(after, before):
    """When an "or" or extra part applies: a token the app can check, or the
    stat block's words (the GM ticks them on the action)."""
    text = before + " " + after
    moved = re.search(r"the ([a-z][a-z \-]*?) moved (\d+)\+ feet straight toward (?:it|the target) "
                      r"immediately before the hit", text)
    if moved:
        return f"the {moved.group(1)} moved {moved.group(2)}+ feet straight toward it immediately before the hit"
    clause = re.search(r"\bif (.+?)(?:\u2014|\.|,|$)", after)
    if not clause:
        clause = re.search(r"\b[Ii]f (.+?),", before)
    if not clause:
        return ""
    words = clause.group(1).strip()
    if re.fullmatch(r"the target is Bloodied", words):
        return "@targetBloodied"
    if re.fullmatch(r"(?:the|this) [a-z ]+ is Bloodied|it is Bloodied", words):
        return "@selfBloodied"
    if re.match(r"the target is Grappled by", words):
        return "@grappledBySelf"
    if "had Advantage" in words:
        return ""
    return words


def damage_parts(segment):
    found = []
    for match in ROLLED_RE.finditer(segment):
        found.append((match.start(), match.end(), dice_text(*match.group(1, 2, 3, 4)),
                      (match.group(5) or "").lower()))
    for match in FLAT_RE.finditer(segment):
        if any(a <= match.start() < b for a, b, _, _ in found):
            continue
        found.append((match.start(), match.end(), match.group(1), match.group(2).lower()))
    found.sort()
    parts = []
    for index, (start, end, dice, damage_type) in enumerate(found):
        left = max(segment.rfind(". ", 0, start), segment.rfind(": ", 0, start))
        left = 0 if left < 0 else left + 2
        if index > 0:
            left = max(left, found[index - 1][1])
        right = segment.find(". ", end)
        right = len(segment) if right < 0 else right
        if index + 1 < len(found):
            right = min(right, found[index + 1][0])
        part = {"dice": dice, "type": damage_type, "when": classify(segment[end:right], segment[left:start])}
        if part["when"] in ("alternative", "conditional"):
            condition = damage_condition(segment[end:right], segment[left:start])
            if condition:
                part["if"] = condition
        parts.append(part)
    return parts


def use_limits(name):
    """Recharge and X/Day from an entry's name: "Petrifying Gaze (Recharge 4–6)"."""
    out = {}
    recharge = re.search(r"\(Recharge (\d)(?:–\d)?\)", name)
    if recharge:
        out["recharge"] = int(recharge.group(1))
    per_day = re.search(r"\((\d+)/Day", name)
    if per_day:
        out["perDay"] = int(per_day.group(1))
    elif "Recharge after a Short or Long Rest" in name:
        out["perDay"] = 1
    return out


def is_multiattack(name):
    """ "Multiattack", or a form-limited one: "Multiattack (Vampire Form Only)". """
    return name == "Multiattack" or name.startswith("Multiattack (")


def per_attack_count(name, multiattack_text):
    """How many of this attack Multiattack names: "makes two Grave Strike attacks" -> 2."""
    base = re.sub(r"\s*\(.*\)$", "", name)
    found = re.search(r"\b(one|two|three|four|five|six|seven|eight)\s+" + re.escape(base) + r"\s+attacks?\b",
                      multiattack_text or "", re.IGNORECASE)
    return NUMBER_WORDS[found.group(1).lower()] if found else None


def structure_attack(attack, multiattack_text):
    effect = attack["effect"]
    name = attack["name"]
    out = {}
    bonus = re.search(r"(?:Melee|Ranged|Melee or Ranged) Attack Roll: ([+\-−])(\d+)", effect)
    save = re.search(r"(Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma) Saving Throw: DC (\d+)", effect)
    if bonus:
        value = int(bonus.group(2))
        out["attackBonus"] = -value if bonus.group(1) != "+" else value
        hit = effect.find("Hit:")
        segment = effect[hit + 4:] if hit >= 0 else ""
        rider = re.search(r"(Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma) Saving Throw:", segment)
        parts = damage_parts(segment[: rider.start()] if rider else segment)
        if rider:
            for part in damage_parts(segment[rider.start():]):
                part["when"] = "conditional"
                parts.append(part)
        out["damage"] = parts
    elif save:
        out["save"] = {"ability": ABILITY_WORDS[save.group(1)], "dc": int(save.group(2)), "onSuccess": "none"}
        failure = effect.find("Failure:")
        success = effect.find("Success:")
        if failure >= 0:
            segment = effect[failure + 8: success if success > failure else len(effect)]
            out["damage"] = damage_parts(segment)
        if success >= 0 and effect[success:].lower().startswith("success: half"):
            out["save"]["onSuccess"] = "half"
        if re.search(r"each creature|each other creature|creatures? in a|\bcone\b|\bline\b|emanation|sphere|cube",
                     effect, re.IGNORECASE):
            out["area"] = True
    out.update(use_limits(name))
    base = re.sub(r"\s*\(.*\)$", "", name)
    if multiattack_text and not is_multiattack(name) and base and base in multiattack_text:
        out["inMultiattack"] = True
    return out


def multiattack_count(monster, text, current):
    if monster["id"] in MULTIATTACK_OVERRIDES:
        return MULTIATTACK_OVERRIDES[monster["id"]]
    # Only the first option counts: "... , or it makes two Hurl Flame attacks" is
    # an alternative, and "It can replace one attack ..." swaps within the total.
    first = text.split(". ")[0].split(", or ")[0]
    words = re.findall(r"\b(one|two|three|four|five|six|seven|eight)\b[^.,;]*?\battacks?\b", first.lower())
    total = sum(NUMBER_WORDS[word] for word in words)
    for use in re.findall(r"\band (?:it )?uses (?:either )?[A-Z][^.,]*", first):
        total += 2 if use.rstrip().endswith("twice") else 1
    return total if total > 0 else current


def defense_list(entries):
    found = []
    for entry in entries:
        lowered = entry.lower() if isinstance(entry, str) else entry.get("index", "")
        for damage_type in DAMAGE_TYPES:
            if re.search(r"\b" + damage_type + r"\b", lowered) and damage_type not in found:
                found.append(damage_type)
    return found


# Conditions an ability gives the monster that uses it, with what ends them.
# The stat-block text says this in prose; these few are written out by hand.
INVISIBILITY_SPELL = {"condition": "invisible", "source": "Invisibility", "concentration": "invisibility",
                      "endsOn": ["attackRoll", "dealsDamage", "anySpell"]}
SELF_EFFECTS = {
    ("imp", "attacks", "Invisibility"): INVISIBILITY_SPELL,
    ("quasit", "attacks", "Invisibility"): INVISIBILITY_SPELL,
    ("sprite", "attacks", "Invisibility"): INVISIBILITY_SPELL,
    ("oni", "bonusActions", "Invisibility"): INVISIBILITY_SPELL,
    ("adult-blue-dragon", "legendaryActions", "Cloaked Flight"): INVISIBILITY_SPELL,
    ("ancient-blue-dragon", "legendaryActions", "Cloaked Flight"): INVISIBILITY_SPELL,
    ("will-o-wisp", "bonusActions", "Vanish"): {"condition": "invisible", "source": "Vanish",
                                                "concentration": "Vanish",
                                                "endsOn": ["attackRoll", "action:Consume Life"]},
    # A trait: Invisible from the start of the fight, and nothing ends it.
    ("invisible-stalker", "traits", "Invisibility"): {"condition": "invisible", "source": "Invisibility trait",
                                                      "endsOn": []},
}

# Traits that make creatures save at the start of their turns, written out by
# hand. "range" and "who" finish the GM's question: "does Aria start its turn
# within 30 feet of the hag? Only a Beast or Humanoid that ...".
AURAS = {
    ("sea-hag", "Vile Appearance"): {
        "ability": "wisdom", "dc": 11, "range": "within 30 feet of the hag",
        "who": "a Beast or Humanoid that can see the hag's true form", "condition": "frightened",
        "creatureTypes": ["beast", "humanoid"],
        "immuneOnSuccess": True, "suppressedBy": ["Illusory Appearance"]},
    ("ghast", "Stench"): {
        "ability": "constitution", "dc": 10, "range": "within 5 feet of the ghast", "condition": "poisoned",
        "immuneOnSuccess": True},
    ("hezrou", "Stench"): {
        "ability": "constitution", "dc": 16, "range": "within 10 feet of the hezrou", "condition": "poisoned"},
    ("pit-fiend", "Fear Aura"): {
        "ability": "wisdom", "dc": 21, "range": "within 20 feet of the pit fiend", "condition": "frightened",
        "immuneOnSuccess": True, "whileActive": True, "enemiesOnly": True},
    ("gibbering-mouther", "Gibbering"): {
        "ability": "wisdom", "dc": 10, "range": "within 20 feet of the mouther", "whileActive": True,
        "failureDie": 8, "failureTable": [
            {"upTo": 4, "text": "does nothing this turn"},
            {"upTo": 6, "text": "takes no action or Bonus Action and uses all its movement to move in a "
                                "random direction"},
            {"upTo": 8, "text": "makes a melee attack against a random creature within its reach, or does "
                                "nothing if it can't"}]},
}

# Who an action can target, and what a failed save does at low Hit Points.
# Matched on the action's name without its "(Recharge ...)" part.
TARGET_RULES = {
    ("sea-hag", "Death Glare"): {"targetCondition": "frightened", "failureHpThreshold": 20,
                                 "failureHpEffect": "dropsToZero"},
    ("solar", "Slaying Bow"): {"failureHpThreshold": 100, "failureHpEffect": "dies"},
    # "One living creature ... that has 0 Hit Points. Failure: The target dies,
    # and the wisp regains 10 (3d6) Hit Points."
    ("will-o-wisp", "Consume Life"): {"targetAtZeroHp": True, "targetExceptTypes": ["undead", "construct"],
                                      "failureHpThreshold": 0, "failureHpEffect": "dies",
                                      "failureSelfHealing": "3d6"},
    ("chuul", "Paralyzing Tentacles"): {"targetCondition": "grappled"},
    ("glabrezu", "Pummel"): {"targetCondition": "grappled"},
    ("kraken", "Swallow"): {"targetCondition": "grappled"},
    ("otyugh", "Tentacle Slam"): {"targetCondition": "grappled"},
    ("succubus", "Draining Kiss"): {"targetCondition": "charmed"},
    ("shambling-mound", "Engulf"): {"targetMaxSize": "Medium"},
}


def apply_target_rules(document, report):
    found = set()
    for monster in document["monsters"]:
        rows = [row for part in ("attacks", "bonusActions", "reactions", "legendaryActions")
                for row in monster.get(part, [])]
        for row in rows:
            key = (monster["id"], re.sub(r"\s*\(.*\)$", "", row["name"]))
            if key in TARGET_RULES:
                row.update(TARGET_RULES[key])
                found.add(key)
    for key in sorted(set(TARGET_RULES) - found):
        report.append(f"no action for target rule {key}")


CREATURE_TYPES = ["Aberration", "Beast", "Celestial", "Construct", "Dragon", "Elemental", "Fey", "Fiend",
                  "Giant", "Humanoid", "Monstrosity", "Ooze", "Plant", "Undead"]
_TYPE = "(" + "|".join(CREATURE_TYPES) + ")s?"


def target_clause(effect):
    """The sentence naming whom a save affects: 'DC 13, each creature in a
    60-foot Cone that can see the ghost and isn't an Undead.'"""
    match = re.search(r"Saving Throw: DC \d+, (.*?)(?:\. (?:Failure|Success|First Failure)|$)", effect)
    return match.group(1) if match else ""


def type_limits(effect):
    """(only these types, never these types) from the target clause:
    'one Humanoid', 'each Humanoid and Giant', 'isn't an Undead or a Construct'."""
    clause = target_clause(effect).replace("\u2019", "'")
    only, never = [], []
    for found in re.finditer(r"isn't (?:an? )?" + _TYPE + r"((?: or (?:an? )?" + _TYPE + r")*)", clause):
        never.append(found.group(1).lower())
        never += [t.lower() for t in re.findall(_TYPE, found.group(2))]
    lead = re.match(r"(?:one|each|up to \w+) " + _TYPE + r"((?:,? (?:and|or) " + _TYPE + r")*)\b", clause)
    if lead:
        only.append(lead.group(1).lower())
        only += [t.lower() for t in re.findall(_TYPE, lead.group(2))]
    return only, never


def apply_type_limits(document, report):
    """Whom a save can affect, by creature type, written onto the action (or
    the aimed feature) so the app turns the rest away: the Ghost's Horrific
    Visage skips Undead; its Possession takes only a Humanoid."""
    count = 0
    for monster in document["monsters"]:
        for part in ("attacks", "bonusActions", "reactions", "legendaryActions"):
            for row in monster.get(part, []):
                aimed = row.get("targeted", row)
                if "save" not in aimed:
                    continue
                only, never = type_limits(row.get("effect", ""))
                if only and not aimed.get("targetTypes"):
                    aimed["targetTypes"] = only
                    count += 1
                if never and not aimed.get("targetExceptTypes"):
                    aimed["targetExceptTypes"] = never
                    count += 1
                if only or never:
                    report.append(f"type limit {monster['id']}/{row['name']}: only {only} never {never}")
    report.append(f"type limits: {count}")


def apply_auras(document, report):
    found = set()
    for monster in document["monsters"]:
        for row in monster.get("traits", []):
            key = (monster["id"], row["name"])
            if key in AURAS:
                row["aura"] = dict(AURAS[key])
                found.add(key)
    for key in sorted(set(AURAS) - found):
        report.append(f"no trait for aura {key}")


def apply_self_effects(document, report):
    found = set()
    for monster in document["monsters"]:
        for section in ("attacks", "traits", "bonusActions", "reactions", "legendaryActions"):
            for row in monster.get(section, []):
                key = (monster["id"], section, row["name"])
                if key in SELF_EFFECTS:
                    row["selfEffect"] = dict(SELF_EFFECTS[key])
                    found.add(key)
    for key in sorted(set(SELF_EFFECTS) - found):
        report.append(f"no ability for self effect {key}")


# --- Conditions an action gives its target -----------------------------------
#
# Read from the stat-block prose: "If the target is a Large or smaller creature,
# it has the Grappled condition (escape DC 14)", "Failure: The target has the
# Frightened condition until the end of the mummy's next turn." The unusual
# ones (a second failure that petrifies, a creature that is swallowed) are
# written out by hand in RIDER_OVERRIDES.

CONDITIONS = ["blinded", "charmed", "deafened", "frightened", "grappled", "incapacitated", "invisible",
              "paralyzed", "petrified", "poisoned", "prone", "restrained", "stunned", "unconscious"]
COND_WORD = r"(?:" + "|".join(c.capitalize() for c in CONDITIONS) + r")"
HAS_RE = re.compile(r"has the (" + COND_WORD + r"(?:(?:,? and |, )" + COND_WORD + r")*) conditions?")
SIZES = ["Tiny", "Small", "Medium", "Large", "Huge", "Gargantuan"]
SIZE_RE = re.compile(r"[Ii]f (?:the )?target is (?:a |an )?(" + "|".join(SIZES) + r") or smaller")
MOVED_RE = re.compile(r"and the ([a-z][a-z \-]*?) moved (\d+)\+ feet straight toward it immediately before the hit")
TIED_PREFIX = {"While Grappled": "grappled", "Until the grapple ends": "grappled", "While Poisoned": "poisoned",
               "While Charmed": "charmed", "While it has the Poisoned condition": "poisoned"}


def condition_ids(words):
    return [w.lower() for w in re.findall(COND_WORD, words)]


def rider_duration(after, rider):
    """Reads "until the start of the assassin's next turn" and the like."""
    clause = re.split(r"\.(?:\s|$)", after, maxsplit=1)[0]
    if re.match(r"\s*\(escape DC (\d+)\)", clause):
        rider["escapeDc"] = int(re.match(r"\s*\(escape DC (\d+)\)", clause).group(1))
    lowered = clause.lower()
    start = re.search(r"until the (start|end) of (?:the )?([a-z’' \-]+?)(?:’s|'s)? next turn", lowered)
    if start:
        whose = start.group(2).strip()
        target = whose in ("its", "target", "the target")
        rider["until"] = ("target" if target else "source") + start.group(1).capitalize()
    elif "until the end of its turn" in lowered:
        rider["until"] = "targetThisTurn"
    elif re.search(r"\bfor 1 minute\b", lowered):
        rider["until"] = "minute"
    if "until the grapple ends" in lowered:
        rider["tiedTo"] = "grappled"
    if "repeats the save at the end of each of its turns" in lowered:
        rider["saveEnds"] = True
    ends = []
    if re.search(r"until the [a-z ]+ dies", lowered):
        ends.append("sourceGone")
    if re.search(r"until it takes damage|ends early if the target takes (?:any )?damage", lowered):
        ends.append("takesDamage")
    if ends:
        rider["endsOn"] = ends


def segment_riders(segment, on):
    riders = []
    sentences = re.split(r"(?<=\.)\s+", segment)
    for sentence in sentences:
        tied = next((cond for prefix, cond in TIED_PREFIX.items() if sentence.startswith(prefix)), None)
        size = SIZE_RE.search(sentence)
        moved = MOVED_RE.search(sentence)
        zero = "reduces the target to 0 Hit Points" in sentence
        matches = list(HAS_RE.finditer(sentence))
        for index, match in enumerate(matches):
            rider = {"conditions": condition_ids(match.group(1)), "on": "zeroHp" if zero else on}
            if size and size.start() < match.start():
                rider["targetMaxSize"] = size.group(1)
            if moved and moved.start() < match.start():
                rider["ask"] = f"the {moved.group(1)} moved {moved.group(2)}+ feet straight toward it " \
                               "immediately before the hit"
            if tied:
                rider["tiedTo"] = tied
            # Up to the next condition: "it has the Grappled condition (escape DC
            # 14), and it has the Restrained condition until the grapple ends."
            stop = matches[index + 1].start() if index + 1 < len(matches) else len(sentence)
            rider_duration(sentence[match.end():stop], rider)
            # "it has the Paralyzed condition until the end of its next turn"
            riders.append(rider)
    return riders


def failure_segments(text):
    """Splits the text after the save line into failure and worse-failure parts."""
    marks = list(re.finditer(r"(First Failure|Second Failure|Subsequent Failures|Failure by 5 or More|"
                             r"Failure or Success|Failure|Success):", text))
    out = {}
    for index, mark in enumerate(marks):
        end = marks[index + 1].start() if index + 1 < len(marks) else len(text)
        out.setdefault(mark.group(1), text[mark.end():end])
    return out


def structure_riders(effect):
    """Returns (riders, riderSave) for an action's text."""
    riders = []
    rider_save = None
    attack = re.search(r"Attack Roll:", effect)
    hit = effect.find("Hit:")
    if attack and hit >= 0:
        body = effect[hit + 4:]
        save = re.search(r"(Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma) Saving Throw: DC (\d+)", body)
        riders += segment_riders(body[: save.start()] if save else body, "hit")
        if save:
            rider_save = {"ability": ABILITY_WORDS[save.group(1)], "dc": int(save.group(2))}
            parts = failure_segments(body[save.end():])
            riders += segment_riders(parts.get("Failure", parts.get("First Failure", "")), "failure")
            lead = body[: save.start()]
            exclude = []
            if re.search(r"non-Undead|isn’t an Undead", lead):
                exclude.append("undead")
            if re.search(r"Undead or elf", lead):
                exclude.append("elf")
            if exclude:
                for rider in riders:
                    if rider["on"] == "failure":
                        rider["exceptTypes"] = exclude
    elif re.search(r"Saving Throw: DC \d+", effect):
        save = re.search(r"Saving Throw: DC \d+", effect)
        parts = failure_segments(effect[save.end():])
        riders += segment_riders(parts.get("Failure", parts.get("First Failure", "")), "failure")
    return riders, rider_save


def target_rules_from_text(effect):
    """'one creature within 5 feet that has the Prone condition', 'one creature Grappled by the chuul'."""
    out = {}
    save = re.search(r"Saving Throw: DC \d+,([^.]*?)\.\s*(?:Failure|First Failure)", effect)
    if not save:
        return out
    clause = save.group(1)
    if not re.match(r"\s*one ", clause):
        return out
    has = re.search(r"that has the (" + COND_WORD + r") condition", clause)
    if has:
        out["targetCondition"] = has.group(1).lower()
    by = re.search(r"(?:(Grappled|Charmed) or )?(Grappled|Charmed) by (?:the|this|it)", clause)
    if by:
        # "Charmed or Grappled by the aboleth": either will do.
        out["targetCondition"] = ",".join(w.lower() for w in by.groups() if w)
    size = re.search(r"one (" + "|".join(SIZES) + r") or smaller creature", clause)
    if size:
        out["targetMaxSize"] = size.group(1)
    return out


INCAP_THEN = lambda worse, **extra: [dict({"conditions": ["incapacitated"], "on": "failure", "saveEnds": True,
                                           "worsensTo": worse}, **extra)]
PETRIFY = [{"conditions": ["restrained"], "on": "failure", "saveEnds": True, "worsensTo": ["petrified"]}]
SWALLOWED = lambda dice, kind, **extra: [dict({"conditions": ["blinded", "restrained"], "on": "failure",
                                               "removes": ["grappled"], "endsOn": ["sourceGone"],
                                               "ongoing": [{"dice": dice, "type": kind}], "ongoingAt": "source"},
                                              **extra)]

# Hand-written riders, by (monster id, entry name without "(Recharge ...)").
RIDER_OVERRIDES = {
    # First failure: Incapacitated, repeat the save at the end of its next turn.
    # A second failure is worse.
    **{(f"{age}-brass-dragon", "Sleep Breath"): [{"conditions": ["incapacitated"], "on": "failure",
                                                  "saveEnds": True,
                                                  "worsensTo": ["unconscious"], "worseEndsOn": ["takesDamage"]}]
       for age in ("adult", "ancient", "young")},
    ("brass-dragon-wyrmling", "Sleep Breath"): [{"conditions": ["incapacitated"], "on": "failure",
                                                 "saveEnds": True,
                                                 "worsensTo": ["unconscious"], "worseEndsOn": ["takesDamage"]}],
    **{(f"{age}-silver-dragon", "Paralyzing Breath"): [{"conditions": ["incapacitated"], "on": "failure",
                                                        "saveEnds": True,
                                                        "worsensTo": ["paralyzed"], "worseSaveEnds": True}]
       for age in ("adult", "ancient", "young")},
    ("silver-dragon-wyrmling", "Paralyzing Breath"): [{"conditions": ["incapacitated"], "on": "failure",
                                                       "saveEnds": True,
                                                       "worsensTo": ["paralyzed"], "worseSaveEnds": True}],
    ("cockatrice", "Petrifying Bite"): PETRIFY,
    ("gorgon", "Petrifying Breath"): PETRIFY,
    ("basilisk", "Petrifying Gaze"): PETRIFY,
    ("medusa", "Petrifying Gaze"): PETRIFY,
    ("homunculus", "Bite"): [
        {"conditions": ["poisoned"], "on": "failure", "until": "sourceEnd"},
        {"conditions": ["poisoned"], "on": "failureBy5", "until": "minute"},
        {"conditions": ["unconscious"], "on": "failureBy5", "tiedTo": "poisoned", "endsOn": ["takesDamage"]}],
    ("pseudodragon", "Sting"): [
        {"conditions": ["poisoned"], "on": "failure"},
        {"conditions": ["poisoned"], "on": "failureBy5"},
        {"conditions": ["unconscious"], "on": "failureBy5", "tiedTo": "poisoned", "endsOn": ["takesDamage"]}],
    ("animated-rug-of-smothering", "Smother"): [
        {"conditions": ["grappled"], "on": "hit", "targetMaxSize": "Medium", "escapeDc": 13,
         "ask": "the rug grapples it instead of dealing damage", "refundDamage": True},
        {"conditions": ["blinded", "restrained"], "on": "hit", "targetMaxSize": "Medium", "tiedTo": "grappled",
         "ask": "the rug grapples it instead of dealing damage",
         "ongoing": [{"dice": "2d6+3", "type": "bludgeoning"}], "ongoingAt": "target"}],
    ("darkmantle", "Crush"): [{"conditions": ["blinded"], "on": "hit", "targetMaxSize": "Medium",
                               "ask": "@advantage", "endsOn": ["sourceGone"]}],
    ("cloaker", "Attach"): [{"conditions": ["blinded"], "on": "hit", "targetMaxSize": "Large",
                             "endsOn": ["sourceGone"]}],
    ("kraken", "Swallow"): [{"conditions": ["restrained"], "on": "failure", "targetMaxSize": "Large",
                             "removes": ["grappled"], "endsOn": ["sourceGone"],
                             "ongoing": [{"dice": "7d6", "type": "acid"}], "ongoingAt": "target"}],
    ("behir", "Swallow"): SWALLOWED("6d6", "acid"),
    ("purple-worm", "Swallow"): SWALLOWED("5d6", "acid"),
    ("tarrasque", "Swallow"): SWALLOWED("16d6", "acid"),
    ("remorhaz", "Swallow"): [{"conditions": ["blinded", "restrained"], "on": "failure", "removes": ["grappled"],
                               "endsOn": ["sourceGone"],
                               "ongoing": [{"dice": "3d6", "type": "acid"}, {"dice": "3d6", "type": "fire"}],
                               "ongoingAt": "source"}],
    ("gelatinous-cube", "Engulf"): [{"conditions": ["restrained"], "on": "failure", "endsOn": ["sourceGone"],
                                     "ongoing": [{"dice": "3d6", "type": "acid"}], "ongoingAt": "source"}],
    ("djinni", "Create Whirlwind"): [{"conditions": ["restrained"], "on": "failure",
                                      "ongoing": [{"dice": "6d6", "type": "thunder"}], "ongoingAt": "target"}],
    ("invisible-stalker", "Vortex"): [{"conditions": ["grappled"], "on": "failure", "escapeDc": 13,
                                       "ongoing": [{"dice": "2d6", "type": "thunder"}], "ongoingAt": "source"}],
    ("shambling-mound", "Engulf"): [
        {"conditions": ["grappled"], "on": "failure", "escapeDc": 14},
        {"conditions": ["blinded", "restrained"], "on": "failure", "tiedTo": "grappled",
         "ongoing": [{"dice": "3d6", "type": "lightning"}], "ongoingAt": "target"}],
    ("water-elemental", "Whelm"): [
        {"conditions": ["grappled"], "on": "failure", "targetMaxSize": "Large", "escapeDc": 14},
        {"conditions": ["restrained"], "on": "failure", "targetMaxSize": "Large", "tiedTo": "grappled",
         "ongoing": [{"dice": "2d8", "type": "bludgeoning"}], "ongoingAt": "source"}],
    ("vrock", "Spores"): [{"conditions": ["poisoned"], "on": "failure", "saveEnds": True,
                           "ongoing": [{"dice": "1d10", "type": "poison"}], "ongoingAt": "target"}],
    ("pit-fiend", "Bite"): [{"conditions": ["poisoned"], "on": "failure", "saveEnds": True,
                             "ongoing": [{"dice": "6d6", "type": "poison"}], "ongoingAt": "target"}],
    ("death-dog", "Bite"): [{"conditions": ["poisoned"], "on": "failure"}],
    ("incubus", "Nightmare"): [{"conditions": ["unconscious"], "on": "failure", "targetMaxHp": 20,
                                "endsOn": ["takesDamage"]}],
    ("nalfeshnee", "Horror Nimbus"): [{"conditions": ["frightened"], "on": "failure", "until": "minute",
                                       "endsOn": ["takesDamage"]}],
    ("phase-spider", "Bite"): [{"conditions": ["poisoned"], "on": "zeroHp", "stabilize": True},
                               {"conditions": ["paralyzed"], "on": "zeroHp", "tiedTo": "poisoned"}],
    ("vampire-familiar", "Umbral Dagger"): [{"conditions": ["poisoned"], "on": "zeroHp", "stabilize": True},
                                            {"conditions": ["paralyzed"], "on": "zeroHp", "tiedTo": "poisoned"}],
    ("chuul", "Paralyzing Tentacles"): [{"conditions": ["poisoned"], "on": "failure", "saveEnds": True},
                                        {"conditions": ["paralyzed"], "on": "failure", "tiedTo": "poisoned"}],
    ("harpy", "Luring Song"): [{"conditions": ["charmed"], "on": "failure", "saveEnds": True},
                               {"conditions": ["incapacitated"], "on": "failure", "tiedTo": "charmed"}],
    ("aboleth", "Dominate Mind"): [{"conditions": ["charmed"], "on": "failure", "endsOn": ["sourceGone"]}],
    ("lamia", "Corrupting Touch"): [{"conditions": ["charmed", "poisoned"], "on": "failure"}],
    ("otyugh", "Bite"): [{"conditions": ["poisoned"], "on": "hit"}],
    ("adult-gold-dragon", "Banish"): [{"conditions": ["incapacitated"], "on": "failure", "until": "sourceStart"}],
    ("ancient-gold-dragon", "Banish"): [{"conditions": ["incapacitated"], "on": "failure", "until": "sourceStart"}],
    ("ghost", "Possession"): [{"conditions": ["incapacitated"], "on": "failure", "endsOn": ["sourceGone"]}],
    ("roper", "Tentacle"): [{"conditions": ["grappled"], "on": "hit", "escapeDc": 14},
                            {"conditions": ["poisoned"], "on": "hit", "tiedTo": "grappled"}],
    ("dretch", "Fetid Cloud"): [{"conditions": ["poisoned"], "on": "failure", "until": "targetEnd"}],
    ("swarm-of-ravens", "Cacophony"): [{"conditions": ["deafened"], "on": "failure", "until": "sourceStart"}],
}

# Hit Point maximum reductions: "decreases by an amount equal to the Necrotic
# damage taken". "type" empty is all the damage taken.
DRAINS = {
    ("clay-golem", "Slam"): {"type": "acid"},
    ("specter", "Life Drain"): {"type": ""},
    ("wraith", "Life Drain"): {"type": ""},
    ("wight", "Life Drain"): {"type": ""},
    ("succubus", "Draining Kiss"): {"type": ""},
    ("vampire", "Bite"): {"type": "necrotic", "heals": True},
    ("vampire-spawn", "Bite"): {"type": "necrotic", "heals": True},
}

# Rider damage the old parse added to the attack as conditional, but that is
# really the ongoing damage of the rider (written in RIDER_OVERRIDES).
DROP_PARTS = {("pit-fiend", "Bite"): "6d6"}


def attack_modifier(name, effect):
    """A trait that changes the monster's attack rolls, read from its words."""
    advantage = re.search(r"\bAdvantage on (?:an |the )?(melee )?attack rolls?", effect)
    disadvantage = re.search(r"\bDisadvantage on (?:ability checks and )?(melee )?attack rolls?", effect)
    if name.startswith("Pack Tactics") and advantage:
        return {"mode": "advantage", "ask": "an ally is within 5 feet of the target and isn't Incapacitated"}
    if advantage and re.search(r"Bloodied", effect):
        out = {"mode": "advantage", "when": "selfBloodied"}
        if advantage.group(1):
            out["meleeOnly"] = True
        return out
    if advantage and re.search(r"doesn\u2019t have all its Hit Points|doesn't have all its Hit Points", effect):
        return {"mode": "advantage", "when": "targetHurt"}
    if advantage and re.search(r"its allies have Advantage on attack rolls", effect):
        reach = re.search(r"(\d+)-foot Emanation originating from the ([a-z ]+?)[,.]", effect)
        out = {"mode": "advantage", "alliesToo": True,
               "ask": f"the attacker is within {reach.group(1)} feet of the {reach.group(2)}" if reach
               else "the attacker is within its aura"}
        if "Incapacitated" in effect:
            out["whileActive"] = True
        return out
    if disadvantage:
        took = re.search(r"takes ([A-Z][a-z]+) damage, it has Disadvantage on attack rolls", effect)
        if took:
            return {"mode": "disadvantage", "when": "afterDamage", "damageType": took.group(1).lower()}
        if re.search(r"in sunlight", effect, re.IGNORECASE):
            return {"mode": "disadvantage", "ask": "it is in sunlight", "sticky": True}
        if re.search(r"in Bright Light", effect):
            return {"mode": "disadvantage", "ask": "it is in Bright Light", "sticky": True}
    return None


BENEFIT_DICE = r"\d+ \((\d+d\d+(?:\s*[+\-]\s*\d+)?)\)"


def benefit_from_text(effect):
    """A helpful action for another creature: "one creature of its choice ...
    gains 16 (2d10 + 5) Temporary Hit Points and has Advantage on attack rolls
    until the start of the giant's next turn"."""
    if not re.search(r"\bor one creature|one creature it can see|another creature|targets itself or", effect):
        return None
    out = {}
    temp = re.search(r"gains? " + BENEFIT_DICE + r" Temporary Hit Points", effect)
    if temp:
        out["tempHp"] = temp.group(1).replace(" ", "")
    heal = re.search(r"(?:target|creature) (?:magically )?regains " + BENEFIT_DICE + r" Hit Points", effect)
    if heal:
        out["healing"] = heal.group(1).replace(" ", "")
    if re.search(r"has Advantage on attack rolls until", effect):
        out["advantageOnAttacks"] = True
    ac = re.search(r"AC increases by (\d+)", effect)
    if ac:
        out["acBonus"] = int(ac.group(1))
    until = re.search(r"until the (start|end) of the [a-z ]+?\u2019s next turn", effect)
    if until:
        out["until"] = "sourceStart" if until.group(1) == "start" else "sourceEnd"
    return out if any(k in out for k in ("tempHp", "healing", "advantageOnAttacks", "acBonus")) else None


BLOODIED_TRIGGER = re.compile(r"after dealing damage to a creature that (?:was|is) already Bloodied")


def apply_benefits(document, report):
    count = 0
    for monster in document["monsters"]:
        for section in ("bonusActions", "reactions", "legendaryActions"):
            for row in monster.get(section, []):
                row.pop("trigger", None)
                if BLOODIED_TRIGGER.search(row["effect"]):
                    row["trigger"] = "damagedBloodied"
        for section in ("attacks", "bonusActions", "reactions", "legendaryActions"):
            for row in monster.get(section, []):
                row.pop("benefit", None)
                if "save" in row or "attackBonus" in row:
                    continue
                benefit = benefit_from_text(row["effect"])
                if benefit:
                    row["benefit"] = benefit
                    count += 1
    report.append(f"benefits: {count}")


def apply_attack_modifiers(document, report):
    count = 0
    for monster in document["monsters"]:
        for row in monster.get("traits", []):
            row.pop("attackModifier", None)
            modifier = attack_modifier(row["name"], row["effect"])
            if modifier:
                row["attackModifier"] = modifier
                count += 1
    report.append(f"attack modifiers: {count}")


def base_name(name):
    return re.sub(r"\s*\(.*\)$", "", name)


def apply_riders(monster, section, row, report):
    key = (monster["id"], base_name(row["name"]))
    for stale in ("riders", "riderSave", "drain", "advantageIfGrappled"):
        row.pop(stale, None)
    if key in RIDER_OVERRIDES:
        riders = [dict(r) for r in RIDER_OVERRIDES[key]]
        _, rider_save = structure_riders(row["effect"])
    else:
        riders, rider_save = structure_riders(row["effect"])
    if riders:
        row["riders"] = riders
    if rider_save and any(r["on"] in ("failure", "failureBy5") for r in riders):
        row["riderSave"] = rider_save
    if key in DRAINS:
        row["drain"] = dict(DRAINS[key])
    if re.search(r"with Advantage if the target is Grappled by", row["effect"]):
        row["advantageIfGrappled"] = True
    if key in DROP_PARTS and "damage" in row:
        row["damage"] = [p for p in row["damage"] if p["dice"] != DROP_PARTS[key]]
    for name, value in target_rules_from_text(row["effect"]).items():
        row.setdefault(name, value)
    return key


# Spells a stat block "casts" by name that work like an aimed save: the save,
# who it can target, and the condition a failure gives.
SPELL_CASTS = {
    "Charm Person": {"ability": "wisdom", "advantageIf": "the caster or its allies are fighting it",
                     "targetTypes": ["humanoid"], "conditions": ["charmed"], "endsOn": ["takesDamage"]},
}


def structure_spell_cast(monster, row, report):
    """ "The vampire casts Charm Person, ... (spell save DC 17)": aimed like an
    action, with the spell's save and condition."""
    if "following spells" in row["effect"]:
        return
    for spell, rules in SPELL_CASTS.items():
        if not re.search(r"\bcasts " + re.escape(spell) + r"\b", row["effect"]):
            continue
        dc = re.search(r"spell save DC (\d+)", row["effect"])
        if not dc:
            report.append(f"no save DC for {monster['id']}/{row['name']} casting {spell}")
            continue
        row["save"] = {"ability": rules["ability"], "dc": int(dc.group(1)), "onSuccess": "none",
                       "advantageIf": rules["advantageIf"]}
        row["targetTypes"] = rules["targetTypes"]
        rider = {"conditions": rules["conditions"], "on": "failure", "endsOn": list(rules["endsOn"])}
        # "a willing recipient of the vampire's Bite, the damage of which
        # doesn't end the spell"
        spared = re.search(r"recipient of the [\w ]+?[’']s ([\w ]+?), the damage of which doesn[’']t end the spell",
                           row["effect"])
        if spared:
            rider["endsOn"].append("spares:" + spared.group(1))
        row["riders"] = [rider]
        report.append(f"spell cast {monster['id']}/{row['name']}: {spell}")


def structure_feature(monster, section, row, multi_text):
    """A bonus action, reaction, or legendary action with a save or attack roll
    can be aimed at targets like an action."""
    for stale in ("attackBonus", "save", "damage", "area", "targetCondition", "targetMaxSize"):
        row.pop(stale, None)
    if not re.search(r"Saving Throw: DC \d+|Attack Roll:", row["effect"]):
        return
    if not re.search(r"Saving Throw: DC \d+,|Attack Roll:", row["effect"]):
        # "Saving Throw: DC 14." with no target line is part of a longer effect
        # (Trampling Charge); still a save the target makes.
        pass
    structured = structure_attack({"name": row["name"], "effect": row["effect"]}, "")
    for name in ("attackBonus", "save", "damage", "area"):
        if name in structured:
            row[name] = structured[name]


def main(path, db_path):
    with open(path, encoding="utf-8") as handle:
        document = json.load(handle)
    with open(db_path, encoding="utf-8") as handle:
        db = {row["index"]: row for row in json.load(handle)}
    names = {monster["name"] for monster in document["monsters"]}
    report = []
    for monster in document["monsters"]:
        for section in ("attacks", "traits", "bonusActions", "reactions", "legendaryActions"):
            for row in monster.get(section, []):
                cleaned = strip_leak(row["effect"], names)
                if cleaned != row["effect"]:
                    report.append(f"stripped {monster['id']}/{row['name']}: {row['effect'][len(cleaned):].strip()}")
                    row["effect"] = cleaned
        multi = next((a for a in monster.get("attacks", []) if is_multiattack(a["name"])), None)
        multi_text = multi["effect"] if multi else ""
        structured = []
        for attack in monster.get("attacks", []):
            row = {"name": attack["name"], "effect": attack["effect"], "count": attack["count"]}
            if is_multiattack(attack["name"]):
                fixed = multiattack_count(monster, attack["effect"], attack["count"])
                if fixed != attack["count"]:
                    report.append(f"multiattack {monster['id']}: {attack['count']} -> {fixed}")
                row["count"] = fixed
            else:
                row.update(structure_attack(attack, multi_text))
                named = per_attack_count(attack["name"], multi_text)
                if row["count"] == 1 and named is not None and named > 1:
                    report.append(f"attack count {monster['id']}/{attack['name']}: 1 -> {named}")
                    row["count"] = named
            structured.append(row)
        monster["attacks"] = structured
        for row in monster.get("legendaryActions", []):
            if row["name"] == "Legendary Action Uses":
                uses = re.match(r"(\d+)", row["effect"])
                if uses:
                    monster["legendaryActionUses"] = int(uses.group(1))
        source = db.get(DB_ALIASES.get(monster["id"], monster["id"]))
        if source is None:
            report.append(f"no 5e-srd-api row for {monster['id']}")
            continue
        monster["damageResistances"] = defense_list(source.get("damage_resistances", []))
        monster["damageImmunities"] = defense_list(source.get("damage_immunities", []))
        monster["damageVulnerabilities"] = defense_list(source.get("damage_vulnerabilities", []))
        monster["conditionImmunities"] = [c["index"] for c in source.get("condition_immunities", [])
                                          if isinstance(c, dict)]
        saves = {}
        for proficiency in source.get("proficiencies", []):
            index = proficiency["proficiency"]["index"]
            if index.startswith("saving-throw-"):
                saves[ABILITIES[index[len("saving-throw-"):]]] = proficiency["value"]
        monster["savingThrows"] = saves
        skills = {}
        for proficiency in source.get("proficiencies", []):
            index = proficiency["proficiency"]["index"]
            if index.startswith("skill-"):
                skills[index[len("skill-"):]] = proficiency["value"]
        monster["skills"] = skills
        if "xp" in source:
            monster["xp"] = source["xp"]
        for key in ("damageResistances", "damageImmunities", "damageVulnerabilities", "conditionImmunities",
                    "savingThrows", "skills"):
            if not monster[key]:
                del monster[key]
    for monster in document["monsters"]:
        for section in ("traits", "bonusActions", "reactions"):
            for row in monster.get(section, []):
                row.update(use_limits(row["name"]))
    rider_keys = set()
    for monster in document["monsters"]:
        multi = next((a for a in monster.get("attacks", []) if is_multiattack(a["name"])), None)
        for row in monster.get("attacks", []):
            if not is_multiattack(row["name"]):
                rider_keys.add(apply_riders(monster, "attacks", row, report))
        for section in ("bonusActions", "reactions", "legendaryActions"):
            for row in monster.get(section, []):
                structure_feature(monster, section, row, multi["effect"] if multi else "")
                rider_keys.add(apply_riders(monster, section, row, report))
                structure_spell_cast(monster, row, report)
    for key in sorted(set(RIDER_OVERRIDES) - rider_keys):
        report.append(f"no entry for rider override {key}")
    for key in sorted(set(DRAINS) - rider_keys):
        report.append(f"no entry for drain {key}")
    apply_attack_modifiers(document, report)
    apply_benefits(document, report)
    apply_self_effects(document, report)
    apply_auras(document, report)
    apply_target_rules(document, report)
    apply_type_limits(document, report)
    document["schemaVersion"] = 2
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print("\n".join(report))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
