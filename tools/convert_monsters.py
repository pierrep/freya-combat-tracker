#!/usr/bin/env python3
"""Converts a monster list in the dnd-data format into a custom monster file
the app opens (custom-monsters.json, schemaVersion 2, source "custom").

The dnd-data format is the one in github.com/nick-aschenbach/dnd-data
(data/monsters.json): a JSON array of

    {"name": "...", "description": "<stat block as one line of prose>",
     "properties": {"Size": "Large", "Type": "aberration", "AC": "17 (Natural Armor)",
                    "HP": "135 (18d10+36)", "Speed": "...", "STR": 21, ...,
                    "Challenge Rating": 10, "Saving Throws": "Con +6, Int +8",
                    "Skills": "History +12", "Senses": "...", "data-XP": "5,900",
                    "data-Traits": "[{\\"Name\\": ..., \\"Desc\\": ...}]", ...},
     "publisher": "...", "book": "..."}

Only convert material you have the right to use: your own homebrew, openly
licensed content, or books you own for your own table.

What it does:

- reads AC, Hit Points and Hit Dice, speed, ability scores, saving throws,
  skills, senses, CR, XP, and damage and condition immunities, resistances and
  vulnerabilities from "properties" (or, failing that, from the prose);
- splits the prose into traits, actions, bonus actions, reactions, and
  legendary actions (or takes them from the data-Traits / data-Actions /
  data-Legendary Actions lists when an entry has them);
- reads attack bonuses, saves, damage, recharge, X/Day, Multiattack counts,
  conditions, and attack-roll modifiers with the same readers that build the
  SRD catalog (tools/build_srd_monsters.py). 2014-style wording ("Melee Weapon
  Attack: +9 to hit", "must succeed on a DC 14 Constitution saving throw or be
  poisoned") is reworded to the 2024 style before it is read; the stored text
  stays as written unless --modern-text is given;
- gives every monster an id that does not clash with the SRD catalog, the
  existing custom monsters, or another converted monster.

Entries without AC, Hit Points, or all six ability scores are skipped (rules
templates, "see the spell" entries) unless --fill-from-open5e finds them or
--allow-incomplete keeps them with placeholders. Lair actions and regional effects are left
out. Anything the readers could not make sense of is listed with --report.

Usage:
  convert_monsters.py INPUT.json -o OUTPUT.json [options]
  convert_monsters.py INPUT.json --merge-into ~/.local/share/combat-tracker/custom-monsters.json [options]

Options:
  --book TEXT          only entries whose book contains TEXT (repeatable)
  --publisher TEXT     only entries whose publisher contains TEXT (repeatable)
  --exclude-book TEXT  leave out entries whose book contains TEXT (repeatable)
  --name REGEX         only entries whose name matches REGEX
  --skip-srd-names     leave out entries named like an SRD monster
  --modern-text        store the reworded 2024-style text instead of the original
  --srd PATH           the SRD catalog (default: ../data/srd/monsters.json)
  --report PATH        write per-monster notes to PATH
  --list-books         print each publisher / book with its entry count, and stop
  --fill-from-open5e   for an entry with no stats, take them from Open5e (api.open5e.com), which has only
                       openly licensed monsters (SRD, Kobold Press, Level Up, ...); needs the same name and book
  --open5e-any-source  with --fill-from-open5e, accept a same-named monster from any Open5e book
  --open5e-cache PATH  where the downloaded Open5e list is kept (default ~/.cache/freya-combat-tracker/)
  --refresh-open5e     download the Open5e list again
  --allow-incomplete   keep entries that still have no stats: AC and Hit Points estimated from CR, ability
                       scores 10, and a "Missing Stats" trait listing what to replace

Open the result in the app by closing the app and replacing custom-monsters.json
in its data folder (or use --merge-into, which keeps a .bak copy).
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.request
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_srd_monsters as srd  # noqa: E402  (the shared stat-block readers)

SCHEMA_VERSION = 2
SOURCE = "custom"
ABILITY_KEYS = ["strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma"]
ABILITY_SHORT = {"str": "strength", "dex": "dexterity", "con": "constitution", "int": "intelligence",
                 "wis": "wisdom", "cha": "charisma"}
ABIL = "Strength|Dexterity|Constitution|Intelligence|Wisdom|Charisma"
TYPES = "|".join(srd.DAMAGE_TYPES)
CONDS = "|".join(srd.CONDITIONS)
SIZES = srd.SIZES
XP_BY_CR = {"0": 10, "1/8": 25, "1/4": 50, "1/2": 100, "1": 200, "2": 450, "3": 700, "4": 1100, "5": 1800,
            "6": 2300, "7": 2900, "8": 3900, "9": 5000, "10": 5900, "11": 7200, "12": 8400, "13": 10000,
            "14": 11500, "15": 13000, "16": 15000, "17": 18000, "18": 20000, "19": 22000, "20": 25000,
            "21": 33000, "22": 41000, "23": 50000, "24": 62000, "25": 75000, "26": 90000, "27": 105000,
            "28": 120000, "29": 135000, "30": 155000}
NUMBER_WORDS = dict(srd.NUMBER_WORDS, nine=9, ten=10)
# --allow-incomplete: Armor Class and Hit Points for a Challenge Rating, from
# the middle of each row of the DMG's monster-creation table. A guess to start
# from, not the monster's real numbers.
ESTIMATE_BY_CR = {"0": (13, 4), "1/8": (13, 21), "1/4": (13, 42), "1/2": (13, 60), "1": (13, 78), "2": (13, 93),
                  "3": (13, 108), "4": (14, 123), "5": (15, 138), "6": (15, 153), "7": (15, 168), "8": (16, 183),
                  "9": (16, 198), "10": (17, 213), "11": (17, 228), "12": (17, 243), "13": (18, 258),
                  "14": (18, 273), "15": (18, 288), "16": (18, 303), "17": (19, 318), "18": (19, 333),
                  "19": (19, 348), "20": (19, 378), "21": (19, 423), "22": (19, 468), "23": (19, 513),
                  "24": (19, 558), "25": (19, 603), "26": (19, 648), "27": (19, 693), "28": (19, 738),
                  "29": (19, 783), "30": (19, 828)}
MISSING_STATS_TRAIT = "Missing Stats"


# --- Small readers ------------------------------------------------------------

def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower().replace("\u2019", "").replace("'", "")).strip("-")


def first_int(value):
    """17, "17 (Natural Armor)", "5,900" -> int; None when there is no number."""
    if isinstance(value, bool) or value is None:
        return None
    if isinstance(value, (int, float)):
        return int(value)
    match = re.search(r"-?\d[\d,]*", str(value))
    return int(match.group(0).replace(",", "")) if match else None


def signed_int(text):
    match = re.search(r"([+\-\u2212\u2013])\s*(\d+)", text)
    if not match:
        return None
    return -int(match.group(2)) if match.group(1) != "+" else int(match.group(2))


def tidy_dice(text):
    """"18d10+36" -> "18d10 + 36", the SRD catalog's spacing."""
    text = text.replace("\u2212", "-").replace("\u2013", "-")
    text = re.sub(r"\s*([+\-])\s*", r" \1 ", text.strip())
    return re.sub(r"\s+", " ", text)


def challenge_text(value):
    if value is None:
        return None
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        fraction = Fraction(value).limit_denominator(8)
        return str(fraction.numerator) if fraction.denominator == 1 else f"{fraction.numerator}/{fraction.denominator}"
    match = re.search(r"\d+(?:/\d+)?(?:\.\d+)?", str(value))
    if not match:
        return None
    text = match.group(0)
    return challenge_text(float(text)) if "." in text else text


def modifier(score):
    return (score - 10) // 2


def bonus_map(text, keys=None):
    """"Con +6, Int +8" -> {"constitution": 6, ...}; "Sleight of Hand +5" ->
    {"sleight-of-hand": 5}."""
    out = {}
    for name, sign, number in re.findall(r"([A-Za-z][A-Za-z ']*?)\s*([+\-\u2212\u2013])\s*(\d+)", text or ""):
        key = name.strip().lower()
        if keys is not None:
            key = keys.get(key[:3]) if key[:3] in keys else None
            if key is None:
                continue
        else:
            key = slug(key)
        out[key] = int(number) if sign == "+" else -int(number)
    return out


def size_text(value):
    words = re.findall("|".join(SIZES), str(value or "").title())
    return words[0] if words else ""


def creature_type(value):
    text = str(value or "").strip()
    return re.sub(r"\b([a-z])", lambda m: m.group(1).upper(), text) if text else ""


# --- Description text ---------------------------------------------------------

def clean_description(text):
    text = text or ""
    # Leftovers from a word-processor export: "@font-face {...}", "p.MsoNormal {mso-...}".
    text = re.sub(r"@font-face\s*\{[^{}]*\}", " ", text)
    text = re.sub(r"[\w.#:\-, ]*\{[^{}]*\}", " ", text)
    text = text.replace("\u00a0", " ")
    return re.sub(r"\s+", " ", text).strip()


HEADING_RE = re.compile(r"(?<![A-Za-z])(Bonus Actions|Legendary Actions|Mythic Actions|Lair Actions|Regional Effects|"
                        r"Reactions|Actions|Traits)(?:\s+\1)*(?=\s+[A-Z])")
SECTION_KEYS = {"Traits": "traits", "Actions": "attacks", "Bonus Actions": "bonusActions",
                "Reactions": "reactions", "Legendary Actions": "legendaryActions"}
# "Dive Attack . If the ..." / "Amphibious : The ...": this source spaces the
# punctuation after an entry's name, which nothing else in a stat block does.
SPACED_NAME_RE = re.compile(r"(?:^|(?<=[.!?)\]\"\u201d\u2019:]\s))([A-Z][^.:!?]{0,70}?)\s+[.:]\s+")
# "Bite. Melee Attack Roll: ..." for text without that spacing: Title Case
# words, an optional "(Recharge 5-6)", and a full stop.
SMALL = r"(?:of|the|and|or|in|on|to|a|an|with|from|for|at|by)"
TIGHT_NAME_RE = re.compile(r"(?:^|(?<=[.!?)]\s))([A-Z][\w'\u2019\-]*(?:\s+(?:" + SMALL + r"|[A-Z][\w'\u2019\-]*)){0,6}"
                           r"(?:\s*\([^()]{1,40}\))?)\.\s+")
STATS_END_RE = re.compile(r"Challenge\s+[\d/]+\s*\([\d,]+\s*XP[^)]*\)|Proficiency Bonus\s*\+\s*\d+|"
                          r"\bCR\s+[\d/]+\s*\(XP[^)]*\)")


def headings(text):
    """Section headings that start a section: at the start or after a sentence
    or a stat line, never inside one ("can't take Reactions until ...")."""
    found = []
    for match in HEADING_RE.finditer(text):
        before = text[: match.start()].rstrip()
        starts_entry = SPACED_NAME_RE.match(text[match.end():].lstrip()) is not None
        # "Legendary Actions" and the like are never words inside a sentence.
        starts_entry = starts_entry or " " in match.group(1)
        if before and before[-1] not in ".!?)\"\u201d" and not before.endswith(match.group(1)) and not starts_entry:
            # "...Challenge 10 (5,900 XP) Actions" ends in ")"; a trait's last
            # sentence ends in "."; anything else is a word inside a sentence.
            continue
        found.append(match)
    return found


def clean_name(name):
    """"Thorn Spray (Recharge 5-6)" -> "(Recharge 5\u20136)", the form the readers know."""
    name = re.sub(r"\bRecharges after\b", "Recharge after", name)
    name = re.sub(r"\(Recharge (\d)\s*[-\u2013\u2212]\s*(\d)\)", "(Recharge \\1\u2013\\2)", name.strip())
    return re.sub(r"\s+", " ", name)


def split_entries(text, allow_tight=True):
    """[(name, effect)], plus the text before the first entry."""
    text = text.strip()
    matches = [m for m in SPACED_NAME_RE.finditer(text) if len(m.group(1).split()) <= 10]
    if not matches and allow_tight:
        matches = [m for m in TIGHT_NAME_RE.finditer(text) if not re.match(r"(Hit|Failure|Success)\b", m.group(1))]
    lead = text[: matches[0].start()].strip() if matches else text.strip()
    entries = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        entries.append((clean_name(match.group(1)), text[match.end(): end].strip()))
    return lead, entries


def sections_from_prose(text):
    """{"traits": [(name, effect)], "attacks": [...], ..., "_legendaryIntro": str,
    "_preamble": str, "_dropped": [heading]}"""
    out = {"_dropped": []}
    marks = headings(text)
    preamble = text[: marks[0].start()] if marks else text
    out["_preamble"] = preamble
    stats_end = list(STATS_END_RE.finditer(preamble))
    if stats_end:
        _, out["traits"] = split_entries(preamble[stats_end[-1].end():])
    else:
        # No stat lines to stop at: the flavor text is everything before the
        # first spaced "Name ." entry, and without one there are no traits.
        _, out["traits"] = split_entries(preamble, allow_tight=False)
    for index, mark in enumerate(marks):
        end = marks[index + 1].start() if index + 1 < len(marks) else len(text)
        body = text[mark.end(): end].strip()
        heading = mark.group(1)
        if heading not in SECTION_KEYS:
            out["_dropped"].append(heading)
            continue
        lead, entries = split_entries(body)
        key = SECTION_KEYS[heading]
        out.setdefault(key, []).extend(entries)
        if key == "legendaryActions" and lead:
            out["_legendaryIntro"] = lead
    return out


def sections_from_data(properties):
    """The data-Traits / data-Actions / ... lists, when an entry has them."""
    out = {}
    for prop, key in (("data-Traits", "traits"), ("data-Actions", "attacks"), ("data-Bonus Actions", "bonusActions"),
                      ("data-Reactions", "reactions"), ("data-Legendary Actions", "legendaryActions")):
        raw = properties.get(prop.lower())
        if raw in (None, "", "[]"):
            continue
        try:
            rows = json.loads(raw) if isinstance(raw, str) else raw
        except json.JSONDecodeError:
            continue
        entries = []
        for row in rows if isinstance(rows, list) else []:
            if not isinstance(row, dict) or not row.get("Name"):
                continue
            entries.append((clean_name(str(row["Name"])), data_entry_text(row)))
        if entries:
            out[key] = entries
    return out


def data_entry_text(row):
    desc = str(row.get("Desc") or "").strip()
    if "Hit Bonus" not in row or re.search(r"\bHit:", desc):
        return desc
    # An attack given as fields: write it the way a stat block would.
    kind = str(row.get("Type") or "Melee").strip()
    attack = str(row.get("Type Attack") or "Weapon Attack").strip()
    bonus = str(row.get("Hit Bonus")).strip().lstrip("+")
    reach = str(row.get("Reach") or "").strip()
    reach_word = "range" if kind.lower().startswith("ranged") else "reach"
    target = str(row.get("Target") or "one target").strip()
    damage = str(row.get("Damage") or "").strip()
    damage_type = str(row.get("Damage Type") or "").strip()
    text = f"{kind} {attack}: +{bonus} to hit, " + (f"{reach_word} {reach}, " if reach else "") + f"{target}."
    if damage:
        parsed = re.fullmatch(r"(\d+)d(\d+)(?:\s*([+\-])\s*(\d+))?", damage.replace(" ", ""))
        average = ""
        if parsed:
            count, sides, sign, extra = parsed.groups()
            value = int(count) * (int(sides) + 1) // 2 + (int(extra) if extra else 0) * (-1 if sign == "-" else 1)
            average = f"{value} "
        text += f" Hit: {average}({damage}) {damage_type} damage."
    if desc:
        text += " " + desc
    return text


# --- 2014 wording to 2024 wording (for reading only) -------------------------

SAVE_RE = re.compile(r"(?P<lead>(?:,\s*|\s+)and\s+)?(?P<who>\b(?:[Ee]ach|[Tt]he|[Tt]hat|[Aa]ny|[Ii]t|[Oo]ne|[Aa]n?)\b"
                     r"[^.,;]*?)\s+must (?:make|succeed on) an? DC (?P<dc>\d+) (?P<ab>" + ABIL +
                     r") saving throw(?P<rest>[^.]*)\.")
SELF_TARGET = re.compile(r"^(?:it|the target|the creature|that creature|the target creature|that target)$", re.I)


def _save_sentence(match):
    who = match.group("who").strip()
    rest = match.group("rest").strip()
    head = f"{match.group('ab')} Saving Throw: DC {match.group('dc')}"
    if not SELF_TARGET.match(who):
        head += f", {who[0].lower()}{who[1:]}"
    lead = ". " if match.group("lead") else ""
    taking = re.match(r",?\s*taking (.*?) on a failed save(?:,?\s*or (.*?) on a successful one)?$", rest)
    if taking:
        text = f"{lead}{head}. Failure: {taking.group(1)}."
        if taking.group(2) and "half" in taking.group(2):
            text += " Success: Half damage."
        return text
    consequence = re.match(r"or\s+(.*)$", rest)
    if consequence:
        fail = re.sub(r"^(?:take|takes)\s+", "", consequence.group(1).strip())
        if not re.match(r"\d", fail):
            fail = "The target " + fail
        return f"{lead}{head}. Failure: {fail}."
    return f"{lead}{head}." + (f" {rest}" if rest else "")


def modernize(text):
    """Rewords 2014 stat-block phrasing so the 2024 readers understand it."""
    if not text:
        return text
    t = text.replace("\u2212", "-")
    t = re.sub(r"\b(Melee or Ranged|Melee|Ranged) (?:Weapon |Spell )?Attack:\s*([+\-]\s*\d+) to hit,?",
               lambda m: f"{m.group(1)} Attack Roll: {m.group(2).replace(' ', '')},", t)
    t = re.sub(r"(\d+ \(\d+d\d+(?:\s*[+\-]\s*\d+)?\)) (" + TYPES + r") damage",
               lambda m: f"{m.group(1)} {m.group(2).capitalize()} damage", t, flags=re.I)
    t = re.sub(r"(?<![\d(])\b(\d+) (" + TYPES + r") damage",
               lambda m: f"{m.group(1)} {m.group(2).capitalize()} damage", t, flags=re.I)
    t = re.sub(r"\b(?:is|are|be|becomes?|falls?)\s+(?:knocked\s+)?(" + CONDS + r")\b(?:\s+condition)?",
               lambda m: f"has the {m.group(1).capitalize()} condition", t, flags=re.I)
    t = re.sub(r"\b[Ww]hile (grappled|poisoned|charmed)\b", lambda m: f"While {m.group(1).capitalize()}", t)
    t = re.sub(r"\bUntil this grapple ends\b", "Until the grapple ends", t)
    t = re.sub(r"\b(dis)?advantage\b", lambda m: "Disadvantage" if m.group(1) else "Advantage", t, flags=re.I)
    t = re.sub(r"\b(temporary )?hit points\b", lambda m: "Temporary Hit Points" if m.group(1) else "Hit Points", t,
               flags=re.I)
    t = SAVE_RE.sub(_save_sentence, t)
    t = re.sub(r"\bOn a failed save,\s*(\w)", lambda m: "Failure: " + m.group(1).upper(), t)
    t = re.sub(r"\bOn a successful save,\s*(\w)", lambda m: "Success: " + m.group(1).upper(), t)
    t = re.sub(r"Success: ([^.]*half as much damage[^.]*)\.", r"Success: Half damage. \1.", t)
    t = re.sub(r"\(Recharge (\d)\s*-\s*(\d)\)", "(Recharge \\1\u2013\\2)", t)
    return re.sub(r"\s+", " ", t).strip()


def is_modern(text):
    return bool(re.search(r"Attack Roll:|Saving Throw: DC \d+", text or ""))


REPEATS_SAVE = re.compile(r"repeat the saving throw at the end of each of its turns", re.I)


# --- Entries ------------------------------------------------------------------

def read_riders(row, text, notes):
    riders, rider_save = srd.structure_riders(text)
    if riders and REPEATS_SAVE.search(text):
        for rider in riders:
            if rider["on"] == "failure":
                rider["saveEnds"] = True
    if riders:
        row["riders"] = riders
    if rider_save and any(r["on"] in ("failure", "failureBy5") for r in riders):
        row["riderSave"] = rider_save
    if re.search(r"with Advantage if the target is Grappled by", text):
        row["advantageIfGrappled"] = True
    for name, value in srd.target_rules_from_text(text).items():
        row.setdefault(name, value)
    if "save" in row:
        only, never = srd.type_limits(text)
        if only:
            row["targetTypes"] = only
        if never:
            row["targetExceptTypes"] = never


def valid_dice(text):
    """What the app's parseDice accepts: "7", "2d6", "2d6+3"; never "1d0" or "0d6"."""
    match = re.fullmatch(r"\s*(\d+)\s*(?:d\s*(\d+)\s*(?:[+\-]\s*\d+)?)?\s*", str(text or "").lower())
    if not match:
        return False
    return match.group(2) is None or (int(match.group(1)) >= 1 and int(match.group(2)) >= 1)


DICE_LISTS = ("damage", "ongoing", "saveFailDamage", "laterDamage")


def drop_bad_dice(value, notes, where=""):
    """Removes, anywhere in a monster, dice the app would refuse to load (a
    stat block's "0 (1d0) damage"); one bad part would stop the whole file."""
    if isinstance(value, list):
        for item in value:
            drop_bad_dice(item, notes, where)
        return
    if not isinstance(value, dict):
        return
    label = value.get("name", where) if isinstance(value.get("name"), str) else where
    for key in DICE_LISTS:
        parts = value.get(key)
        if isinstance(parts, list):
            kept = [part for part in parts if isinstance(part, dict) and valid_dice(part.get("dice"))]
            for part in parts:
                if part not in kept:
                    notes.append(f"\"{label}\": dropped damage \"{part.get('dice') if isinstance(part, dict) else part}\""
                                 " (not dice the app can roll)")
            value[key] = kept
    benefit = value.get("benefit")
    if isinstance(benefit, dict):
        for key in ("tempHp", "healing"):
            if key in benefit and not valid_dice(benefit[key]):
                notes.append(f"\"{label}\": dropped {key} \"{benefit.pop(key)}\" (not dice the app can roll)")
    for key, child in value.items():
        if key not in DICE_LISTS and isinstance(child, (list, dict)):
            drop_bad_dice(child, notes, label)


def plain_damage(row):
    """Drops damage parts the app would refuse."""
    if "damage" in row:
        row["damage"] = [p for p in row["damage"] if valid_dice(p["dice"])]


def multiattack_share(base, multi_text):
    """How many attacks Multiattack lets this one make: "makes two Rend
    attacks" -> 2, "makes three Shortsword or Light Crossbow attacks" -> 3,
    "makes two attacks, using Scimitar or Pistol in any combination" -> 2."""
    if not base or not multi_text:
        return None
    words = "|".join(NUMBER_WORDS)
    name = re.escape(base)
    for match in re.finditer(r"\b(" + words + r")\s+([^.]*?)\battacks?\b(?:,?\s*using ([^.]*))?", multi_text, re.I):
        group = match.group(2) + " " + (match.group(3) or "")
        if re.search(r"\b" + name + r"\b", group, re.I):
            return NUMBER_WORDS[match.group(1).lower()]
    return None


def convert_action(name, effect, multi_text, notes):
    text = effect if is_modern(effect) else modernize(effect)
    row = {"name": name, "count": 1}
    if srd.is_multiattack(name):
        row["count"] = max(1, srd.multiattack_count({"id": ""}, text, 1))
        return row, text
    base = srd.base_name(name)
    if multi_text and base and re.search(r"\b" + re.escape(base) + r"\b", multi_text, re.I):
        row["inMultiattack"] = True
    named = multiattack_share(base, multi_text)
    if named and named > 1:
        row["count"] = named
    if base.startswith("Spellcasting"):
        return row, text
    row.update(srd.structure_attack({"name": name, "effect": text}, ""))
    read_riders(row, text, notes)
    plain_damage(row)
    if "attackBonus" not in row and "save" not in row and re.search(r"attack|saving throw", effect, re.I):
        notes.append(f"action \"{name}\": no attack roll or save read")
    return row, text


def convert_feature(section, name, effect, notes):
    text = effect if is_modern(effect) else modernize(effect)
    row = {"name": name}
    row.update(srd.use_limits(name))
    if section == "traits":
        modifier_row = srd.attack_modifier(name, text)
        if modifier_row:
            row["attackModifier"] = modifier_row
        return row, text
    if section == "legendaryActions":
        row.pop("recharge", None)
        row.pop("perDay", None)
    if srd.BLOODIED_TRIGGER.search(text):
        row["trigger"] = "damagedBloodied"
    probe = {"name": name, "effect": text}
    srd.structure_feature(None, section, probe, "")
    for key in ("attackBonus", "save", "damage", "area"):
        if key in probe:
            row[key] = probe[key]
    if "save" in row or "attackBonus" in row:
        read_riders(row, text, notes)
        plain_damage(row)
    else:
        benefit = srd.benefit_from_text(text)
        if benefit:
            row["benefit"] = benefit
    return row, text


LEGENDARY_USES_RE = re.compile(r"can take (\d+|" + "|".join(NUMBER_WORDS) + r") legendary actions", re.I)


def legendary_uses(intro, properties):
    found = first_int(properties.get("data-lanum"))
    if found:
        return found
    match = LEGENDARY_USES_RE.search(intro or "")
    if match:
        word = match.group(1).lower()
        return int(word) if word.isdigit() else NUMBER_WORDS[word]
    match = re.match(r"\s*(\d+)\b", intro or "")
    return int(match.group(1)) if match else 0


# --- Monsters -----------------------------------------------------------------

def prose_value(text, label, stop=r"Armor Class|Hit Points|Speed|STR\b|Saving Throws|Skills|Damage \w+|"
                                     r"Condition Immunities|Senses|Languages|Challenge|Proficiency Bonus|$"):
    match = re.search(r"\b" + label + r"\s*:?\s*(.+?)\s*(?=\b(?:" + stop + r"))", text)
    return match.group(1).strip(" ;,") if match else ""


def abilities_from(properties, preamble):
    scores = {}
    for short, key in ABILITY_SHORT.items():
        value = first_int(properties.get(short))
        if value is not None:
            scores[key] = value
    if len(scores) == 6:
        return scores
    match = re.search(r"STR\s+DEX\s+CON\s+INT\s+WIS\s+CHA\s+((?:\d+\s*\(\s*[+\-\u2212\u2013]?\s*\d+\s*\)\s*){6})",
                      preamble)
    if match:
        values = [int(v) for v in re.findall(r"(\d+)\s*\(", match.group(1))]
        return dict(zip(ABILITY_KEYS, values))
    return scores


def defenses(properties, preamble):
    out = {}
    for label, key in (("Damage Resistances", "damageResistances"), ("Damage Immunities", "damageImmunities"),
                       ("Damage Vulnerabilities", "damageVulnerabilities")):
        text = str(properties.get(label.lower()) or prose_value(preamble, label))
        found = srd.defense_list([text]) if text else []
        if found:
            out[key] = found
            if re.search(r"nonmagical|non-magical|silvered|adamantine|magic weapons", text, re.I):
                out.setdefault("_notes", []).append(f"{label} \"{text}\" kept as plain {', '.join(found)}")
    text = str(properties.get("condition immunities") or prose_value(preamble, "Condition Immunities"))
    conditions = [c for c in srd.CONDITIONS if re.search(r"\b" + c + r"\b", text.lower())]
    if conditions:
        out["conditionImmunities"] = conditions
    return out


def convert_monster(entry, notes, modern_text, allow_incomplete=False):
    name = str(entry.get("name") or "").strip()
    properties = {str(k).lower(): v for k, v in (entry.get("properties") or {}).items()}
    description = clean_description(entry.get("description"))
    prose = sections_from_prose(description)
    preamble = prose["_preamble"]

    ac = first_int(properties.get("data-acnum")) or first_int(properties.get("ac")) or \
        first_int(prose_value(preamble, "Armor Class"))
    hp_text = str(properties.get("hp") or prose_value(preamble, "Hit Points") or "")
    hp = first_int(properties.get("data-hpnum")) or first_int(hp_text)
    abilities = abilities_from(properties, preamble)
    missing = [what for what, ok in (("AC", ac), ("Hit Points", hp), ("ability scores", len(abilities) == 6)) if not ok]
    placeholders = []
    if missing:
        if not allow_incomplete:
            return None, "the source has no " + ", ".join(missing) + " for it"
        rating = challenge_text(properties.get("challenge rating")) or \
            challenge_text(properties.get("data-crnum")) or "0"
        guess_ac, guess_hp = ESTIMATE_BY_CR.get(rating, ESTIMATE_BY_CR["0"])
        if not ac:
            ac = guess_ac
            placeholders.append(f"AC {ac}")
        if not hp:
            hp = guess_hp
            placeholders.append(f"Hit Points {hp}")
        if len(abilities) < 6:
            placeholders.append("ability scores " + ", ".join(
                key[:3].upper() for key in ABILITY_KEYS if key not in abilities) + " (10)")
            abilities = {key: abilities.get(key, 10) for key in ABILITY_KEYS}
        notes.append("not in the source, filled with placeholders: " + "; ".join(placeholders))

    dice = re.search(r"\(([^)]*\d+d\d+[^)]*)\)", hp_text)
    cr = challenge_text(properties.get("challenge rating")) or challenge_text(properties.get("data-crnum")) or \
        challenge_text(prose_value(preamble, "Challenge"))
    if cr is None:
        notes.append("no challenge rating; set to 0")
        cr = "0"
    skills = bonus_map(str(properties.get("skills") or prose_value(preamble, "Skills")))
    senses = str(properties.get("senses") or prose_value(preamble, "Senses"))
    passive = first_int(properties.get("passive perception"))
    if passive is None:
        found = re.search(r"passive Perception\s*(\d+)", senses + " " + preamble, re.I)
        passive = int(found.group(1)) if found else 10 + skills.get("perception", modifier(abilities["wisdom"]))
    initiative = properties.get("initiative")
    initiative = signed_int(str(initiative)) if initiative is not None else None
    if initiative is None:
        initiative = modifier(abilities["dexterity"])

    monster = {
        "id": "",
        "name": name,
        "size": size_text(properties.get("size")) or "Medium",
        "creatureType": creature_type(properties.get("type")),
        "ac": ac,
        "hp": hp,
        "hitDice": tidy_dice(dice.group(1)) if dice else "",
        "speed": str(properties.get("speed") or prose_value(preamble, "Speed")).strip(),
        "initiativeBonus": initiative,
        "abilities": {key: abilities[key] for key in ABILITY_KEYS},
        "passivePerception": passive,
        "challengeRating": cr,
        "source": SOURCE,
    }
    if not properties.get("size"):
        notes.append("no size; set to Medium")

    data = sections_from_data(properties)
    sections = {key: data.get(key) or prose.get(key) or [] for key in SECTION_KEYS.values()}
    for heading in prose["_dropped"]:
        notes.append(f"left out {heading}")

    attacks = sections["attacks"]
    multi = next((effect for n, effect in attacks if srd.is_multiattack(n)), "")
    multi_text = multi if is_modern(multi) else modernize(multi)
    rows = []
    for entry_name, effect in attacks:
        row, text = convert_action(entry_name, effect, multi_text, notes)
        row["effect"] = text if modern_text else effect
        rows.append(row)
    if rows:
        monster["attacks"] = [dict({"name": r["name"], "effect": r["effect"], "count": r["count"]},
                                   **{k: v for k, v in r.items() if k not in ("name", "effect", "count")})
                              for r in rows]
    for key in ("traits", "bonusActions", "reactions", "legendaryActions"):
        out = []
        if key == "legendaryActions" and sections[key]:
            intro = prose.get("_legendaryIntro") or str(properties.get("legendary intro") or "")
            uses = legendary_uses(intro, properties)
            if uses:
                monster["legendaryActionUses"] = uses
                out.append({"name": "Legendary Action Uses", "effect": f"{uses}. {intro}".strip()
                            if intro and not intro.startswith(str(uses)) else (intro or str(uses))})
        for entry_name, effect in sections[key]:
            row, text = convert_feature(key, entry_name, effect, notes)
            row = dict({"name": row["name"], "effect": text if modern_text else effect},
                       **{k: v for k, v in row.items() if k != "name"})
            out.append(row)
        if out:
            monster[key] = out
    if placeholders:
        guessed = "; ".join(placeholders)
        monster["traits"] = [{"name": MISSING_STATS_TRAIT,
                              "effect": f"The source file has no stats for this monster. Placeholders to replace: "
                                        f"{guessed}. AC and Hit Points are estimates for CR "
                                        f"{monster['challengeRating']}."}] + monster.get("traits", [])
    if not rows and not sections["traits"]:
        notes.append("no traits or actions found in the text")

    found_defenses = defenses(properties, preamble)
    notes.extend(found_defenses.pop("_notes", []))
    monster.update(found_defenses)
    saves = bonus_map(str(properties.get("saving throws") or prose_value(preamble, "Saving Throws")), ABILITY_SHORT)
    if saves:
        monster["savingThrows"] = saves
    if skills:
        monster["skills"] = skills
    xp = first_int(properties.get("data-xp"))
    if xp is None:
        found = re.search(r"\(([\d,]+)\s*XP", preamble)
        xp = int(found.group(1).replace(",", "")) if found else XP_BY_CR.get(cr)
    if xp is not None:
        monster["xp"] = xp
    drop_bad_dice(monster, notes)
    return monster, None


# --- Open5e ---------------------------------------------------------------------
#
# --fill-from-open5e looks a stat-less entry up in Open5e (api.open5e.com),
# which serves openly licensed monsters only (OGL, CC-BY-4.0, ORC): the SRD,
# Kobold Press's Tome of Beasts 1-3, Creature Codex and Deep Magic, Level Up's
# Monstrous Menagerie, and a few more. A match needs the same name and, unless
# --open5e-any-source is given, the same book.

OPEN5E_URL = "https://api.open5e.com/v1/monsters/?format=json&limit=500"
OPEN5E_KEEP = ("slug", "name", "size", "type", "subtype", "alignment", "armor_class", "armor_desc", "hit_points",
               "hit_dice", "speed", "strength", "dexterity", "constitution", "intelligence", "wisdom", "charisma",
               "strength_save", "dexterity_save", "constitution_save", "intelligence_save", "wisdom_save",
               "charisma_save", "perception", "skills", "damage_vulnerabilities", "damage_resistances",
               "damage_immunities", "condition_immunities", "senses", "languages", "challenge_rating", "cr",
               "actions", "bonus_actions", "reactions", "legendary_desc", "legendary_actions", "special_abilities",
               "document__slug", "document__title", "document__license_url")
# Book names in the input file -> Open5e document slugs, for names that differ.
BOOK_ALIASES = {
    "free basic rules": ["wotc-srd"], "basic rules": ["wotc-srd"], "srd": ["wotc-srd"],
    "system reference document": ["wotc-srd"], "5e core rules": ["wotc-srd"],
    "tome of beasts": ["tob", "tob-2023"], "tome of beasts 1": ["tob-2023", "tob"],
    "deep magic": ["dmag", "dmag-e"], "deep magic extended": ["dmag-e"],
    "monstrous menagerie": ["menagerie"], "level up monstrous menagerie": ["menagerie"],
    "taldorei campaign setting": ["taldorei"], "critical role taldorei campaign setting": ["taldorei"],
    "black flag": ["blackflag"],
}
BOOK_NOISE = {"5e", "5th", "edition", "for", "the", "a", "advanced", "reborn"}


def default_open5e_cache():
    base = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "freya-combat-tracker", "open5e-monsters.json")


def download_open5e(path):
    """Every Open5e monster, one page at a time, saved to path."""
    rows, url, page = [], OPEN5E_URL, 0
    while url:
        page += 1
        request = urllib.request.Request(url, headers={"User-Agent": "freya-combat-tracker/convert_monsters.py",
                                                       "Accept": "application/json"})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = json.load(response)
        rows += [{key: row.get(key) for key in OPEN5E_KEEP} for row in data.get("results", [])]
        print(f"open5e: page {page}, {len(rows)} of {data.get('count', '?')} monsters", file=sys.stderr)
        url = data.get("next")
        if url:
            time.sleep(0.5)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(rows, handle, ensure_ascii=False)
    return rows


def load_open5e(path, refresh):
    if refresh or not os.path.exists(path):
        print(f"open5e: downloading the monster list to {path}", file=sys.stderr)
        return download_open5e(path)
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def name_keys(name):
    """"Aboleth, Nihileth" also matches "Nihileth Aboleth" and "Nihileth"."""
    plain = re.sub(r"\s*\([^)]*\)\s*$", "", name)
    keys = {slug(plain)}
    if "," in plain:
        first, rest = [part.strip() for part in plain.split(",", 1)]
        keys |= {slug(f"{rest} {first}"), slug(rest)}
    return {key for key in keys if key}


def book_key(title):
    title = re.sub(r"\([^)]*\)", " ", str(title or "").lower()).replace("\u2019", "").replace("'", "")
    title = re.sub(r"\bvol(?:ume)?\.?\s*\d+\b", " ", title)
    return " ".join(word for word in re.findall(r"[a-z0-9]+", title) if word not in BOOK_NOISE)


class Open5eIndex:
    def __init__(self, rows):
        self.by_name = {}
        self.slugs_by_book = {}
        for row in rows:
            if not isinstance(row, dict) or not row.get("name"):
                continue
            for key in name_keys(row["name"]):
                self.by_name.setdefault(key, []).append(row)
            self.slugs_by_book.setdefault(book_key(row.get("document__title")), set()).add(row.get("document__slug"))

    def documents_for(self, book):
        key = book_key(book)
        found = list(self.slugs_by_book.get(key, ()))
        for alias, slugs in BOOK_ALIASES.items():
            if key == book_key(alias) or key.startswith(book_key(alias) + " basic rules"):
                found += [s for s in slugs if s not in found]
        if not found and key.startswith("free basic rules"):
            found = ["wotc-srd"]
        return found

    def find(self, name, book, any_source):
        candidates = []
        for key in name_keys(name):
            candidates += [row for row in self.by_name.get(key, []) if row not in candidates]
        if not candidates:
            return None
        documents = self.documents_for(book)
        for document in documents:
            for row in candidates:
                if row.get("document__slug") == document:
                    return row
        if any_source:
            # The SRD first, then whatever else has it.
            candidates.sort(key=lambda row: row.get("document__slug") != "wotc-srd")
            return candidates[0]
        return None


def open5e_speed(speed):
    if isinstance(speed, str) or not isinstance(speed, dict):
        return str(speed or "")
    parts = []
    if speed.get("walk") is not None:
        parts.append(f"{speed['walk']} ft.")
    for kind, value in speed.items():
        if kind in ("walk", "hover", "notes") or not isinstance(value, (int, float)):
            continue
        parts.append(f"{kind} {value} ft." + (" (hover)" if kind == "fly" and speed.get("hover") else ""))
    return ", ".join(parts)


def open5e_properties(row):
    """An Open5e monster in the input file's "properties" shape."""
    def entries(key):
        rows = row.get(key) or []
        return json.dumps([{"Name": r.get("name", ""), "Desc": r.get("desc", "")} for r in rows
                           if isinstance(r, dict) and r.get("name")])

    saves = ", ".join(f"{short.capitalize()} {row[key + '_save']:+d}" for short, key in ABILITY_SHORT.items()
                      if isinstance(row.get(key + "_save"), int))
    skills = ", ".join(f"{name.replace('_', ' ').title()} {value:+d}"
                       for name, value in (row.get("skills") or {}).items() if isinstance(value, int))
    kind = str(row.get("type") or "")
    if row.get("subtype"):
        kind += f" ({row['subtype']})"
    out = {
        "size": row.get("size"), "type": kind, "alignment": row.get("alignment"),
        "ac": f"{row.get('armor_class')}" + (f" ({row['armor_desc']})" if row.get("armor_desc") else ""),
        "hp": f"{row.get('hit_points')}" + (f" ({row['hit_dice']})" if row.get("hit_dice") else ""),
        "speed": open5e_speed(row.get("speed")), "saving throws": saves, "skills": skills,
        "senses": row.get("senses") or "", "languages": row.get("languages") or "",
        "damage vulnerabilities": row.get("damage_vulnerabilities") or "",
        "damage resistances": row.get("damage_resistances") or "",
        "damage immunities": row.get("damage_immunities") or "",
        "condition immunities": row.get("condition_immunities") or "",
        "challenge rating": row.get("cr") if row.get("cr") is not None else row.get("challenge_rating"),
        "data-traits": entries("special_abilities"), "data-actions": entries("actions"),
        "data-bonus actions": entries("bonus_actions"), "data-reactions": entries("reactions"),
        "data-legendary actions": entries("legendary_actions"),
        "legendary intro": row.get("legendary_desc") or "",
    }
    for short, key in ABILITY_SHORT.items():
        out[short] = row.get(key)
    found = LEGENDARY_USES_RE.search(row.get("legendary_desc") or "")
    if found:
        word = found.group(1).lower()
        out["data-lanum"] = int(word) if word.isdigit() else NUMBER_WORDS[word]
    return out


def fill_from_open5e(entry, row, same_book):
    """A copy of the entry with Open5e's stats where the entry has none. With
    the same book, Open5e's traits and actions replace the entry's too (they
    are the same monster, and Open5e's are already split into entries)."""
    filled = dict(entry)
    properties = {str(k).lower(): v for k, v in (entry.get("properties") or {}).items()}
    extra = open5e_properties(row)
    for key, value in extra.items():
        if key.startswith("data-") or key == "legendary intro":
            if same_book and value not in ("", "[]", None):
                properties[key] = value
        elif properties.get(key) in (None, "", "None", "\u2014") and value not in (None, ""):
            properties[key] = value
    if same_book and any(properties.get(k) not in (None, "", "[]") for k in ("data-actions", "data-traits")):
        filled["description"] = ""  # the lists replace the prose
    filled["properties"] = properties
    return filled


def unique_id(name, book, taken):
    base = slug(name) or "monster"
    candidates = [base]
    if book:
        candidates.append(f"{base}-{slug(book)}")
    for candidate in candidates:
        if candidate not in taken:
            return candidate
    number = 2
    while f"{candidates[-1]}-{number}" in taken:
        number += 1
    return f"{candidates[-1]}-{number}"


def load_entries(path):
    with open(path, encoding="utf-8") as handle:
        document = json.load(handle)
    if isinstance(document, dict):
        document = document.get("monsters") or document.get("data") or []
    if not isinstance(document, list):
        sys.exit(f"{path}: expected a JSON array of monsters.")
    return [row for row in document if isinstance(row, dict)]


def matches(entry, args):
    book = str(entry.get("book") or "").lower()
    publisher = str(entry.get("publisher") or "").lower()
    if args.book and not any(b.lower() in book for b in args.book):
        return False
    if args.publisher and not any(p.lower() in publisher for p in args.publisher):
        return False
    if args.exclude_book and any(b.lower() in book for b in args.exclude_book):
        return False
    if args.name and not re.search(args.name, str(entry.get("name") or ""), re.I):
        return False
    return True


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input")
    target = parser.add_mutually_exclusive_group()
    target.add_argument("-o", "--output")
    target.add_argument("--merge-into")
    parser.add_argument("--book", action="append", default=[])
    parser.add_argument("--publisher", action="append", default=[])
    parser.add_argument("--exclude-book", action="append", default=[])
    parser.add_argument("--name")
    parser.add_argument("--skip-srd-names", action="store_true")
    parser.add_argument("--modern-text", action="store_true")
    parser.add_argument("--srd", default=os.path.join(here, "..", "data", "srd", "monsters.json"))
    parser.add_argument("--report")
    parser.add_argument("--list-books", action="store_true")
    parser.add_argument("--fill-from-open5e", action="store_true")
    parser.add_argument("--open5e-any-source", action="store_true")
    parser.add_argument("--open5e-cache", default=default_open5e_cache())
    parser.add_argument("--refresh-open5e", action="store_true")
    parser.add_argument("--allow-incomplete", action="store_true")
    args = parser.parse_args(argv)

    entries = load_entries(args.input)
    if args.list_books:
        counts = {}
        for entry in entries:
            key = (str(entry.get("publisher") or "?"), str(entry.get("book") or "?"))
            counts[key] = counts.get(key, 0) + 1
        for (publisher, book), count in sorted(counts.items()):
            print(f"{count:6}  {publisher} / {book}")
        return 0
    if not args.output and not args.merge_into:
        parser.error("give -o OUTPUT or --merge-into FILE")

    open5e = None
    if args.fill_from_open5e:
        try:
            open5e = Open5eIndex(load_open5e(args.open5e_cache, args.refresh_open5e))
        except (OSError, ValueError) as error:
            sys.exit(f"open5e: could not get the monster list ({error}). Try again, or pass --open5e-cache FILE.")
    filled_from = {}

    taken = set()
    srd_names = set()
    if os.path.exists(args.srd):
        with open(args.srd, encoding="utf-8") as handle:
            for monster in json.load(handle).get("monsters", []):
                taken.add(monster["id"])
                srd_names.add(monster["name"].lower())
    else:
        print(f"warning: no SRD catalog at {args.srd}; ids are not checked against it", file=sys.stderr)

    existing = []
    if args.merge_into and os.path.exists(args.merge_into):
        with open(args.merge_into, encoding="utf-8") as handle:
            current = json.load(handle)
        existing = current.get("monsters", [])
        taken.update(row.get("id", "") for row in existing)

    converted, report, skipped, updated, no_stats, incomplete = [], [], 0, 0, 0, 0
    existing_index = {row.get("id", ""): index for index, row in enumerate(existing)}
    this_run = set()
    for entry in entries:
        if not matches(entry, args):
            continue
        name = str(entry.get("name") or "").strip()
        if not name:
            continue
        plain_name = re.sub(r"\s*\(.*\)$", "", name).lower()
        if args.skip_srd_names and plain_name in srd_names:
            continue
        notes = []
        try:
            monster, reason = convert_monster(entry, notes, args.modern_text)
            if monster is None and open5e is not None and reason.startswith("the source has no"):
                row = open5e.find(name, entry.get("book"), args.open5e_any_source)
                if row is not None:
                    same_book = row.get("document__slug") in open5e.documents_for(entry.get("book"))
                    notes = [f"stats from Open5e: {row.get('name')} ({row.get('document__title')}, "
                             f"{row.get('document__license_url') or 'open license'})"]
                    monster, reason = convert_monster(fill_from_open5e(entry, row, same_book), notes,
                                                      args.modern_text)
                    if monster is not None:
                        title = row.get("document__title") or "?"
                        filled_from[title] = filled_from.get(title, 0) + 1
            if monster is None and args.allow_incomplete and reason.startswith("the source has no"):
                monster, reason = convert_monster(entry, notes, args.modern_text, allow_incomplete=True)
                if monster is not None:
                    incomplete += 1
        except Exception as error:  # one bad entry should not stop the rest
            monster, reason = None, f"could not be read ({type(error).__name__}: {error})"
        label = f"{name} [{entry.get('book') or '?'}]"
        if monster is None:
            skipped += 1
            if reason.startswith("the source has no"):
                no_stats += 1
            report.append(f"SKIPPED {label}: {reason}")
            continue
        # Converting the same monster again updates the copy made last time.
        base = slug(name) or "monster"
        again = next((c for c in (base, f"{base}-{slug(entry.get('book') or '')}")
                      if c in existing_index and c not in this_run and existing[existing_index[c]].get("name") == name),
                     None)
        if again:
            monster["id"] = again
            existing[existing_index[again]] = monster
            updated += 1
        else:
            monster["id"] = unique_id(name, entry.get("book"), taken)
            taken.add(monster["id"])
            converted.append(monster)
        this_run.add(monster["id"])
        for note in notes:
            report.append(f"{label}: {note}")

    document = {"schemaVersion": SCHEMA_VERSION, "monsters": existing + converted}
    path = args.merge_into or args.output
    if args.merge_into and os.path.exists(args.merge_into):
        os.replace(args.merge_into, args.merge_into + ".bak")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(document, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    if args.report:
        with open(args.report, "w", encoding="utf-8") as handle:
            handle.write("\n".join(report) + "\n")
    print(f"converted {len(converted) + updated} monsters ({updated} updated in place), skipped {skipped}"
          + (f", kept {len(existing) - updated} existing" if existing else "") + f"; wrote {path}", file=sys.stderr)
    for title, count in sorted(filled_from.items()):
        print(f"open5e: filled {count} from {title}", file=sys.stderr)
    if incomplete:
        print(f"{incomplete} monsters have placeholder stats; each has a \"{MISSING_STATS_TRAIT}\" trait saying "
              "what to replace", file=sys.stderr)
    if no_stats:
        print(f"{no_stats} of the skipped entries have no AC, Hit Points, or ability scores in the source file; "
              "there is nothing to convert them from", file=sys.stderr)
    if report and not args.report:
        print(f"{len(report)} notes; use --report FILE to see them", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
