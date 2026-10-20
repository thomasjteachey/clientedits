"""Build a GameTips.dbc of Centurion loading-screen tips.

Layout (3.3.5): ID, Text_Lang[16] (string offsets), Text_Lang flags = 18 uint32.
Usage: python build_gametips.py <reference GameTips.dbc> <out.dbc>
"""
import struct
import sys

PREFIX = '|cffffd100Tip:|r '

TIPS = [
    # Death and gear
    "When a world character dies in the open world, its green and better equipped gear drops in a cache that anyone can loot. Dungeons, raids, battlegrounds and arenas are safe.",
    "Grey and white items, shirts, tabards and your class Insignia never drop when you die.",
    "Every innkeeper gives out your class Insignia for free.",
    "Dying in the Gurubashi Arena never costs you your gear.",
    "Tournament characters never lose their gear when they die.",
    # War Mode, bounties, world PvP
    "Speak to Grix Warbrand, the War Mode Registrar, to sign up for War Mode.",
    "War Mode doubles the experience and gold you earn in the open world, in exchange for the risk of being attacked by other players.",
    "In a zone below your level, War Mode is paused: you can neither harm nor help other players there, and you earn no experience.",
    "Killing other players in the open world awards experience, though killing the same player again and again awards less.",
    "Every player you kill adds to your Bounty. The bigger your Bounty, the more gold you leave behind when you fall, and no immunity can remove it.",
    "Check the bounty board in any contested-zone inn to see who is out there, and what gear they could drop.",
    "Type /mokgora to challenge someone to Mok'gora, a duel to the death. Both players must type I AGREE before it begins.",
    "Run away from a Mok'gora duel or log out during one, and you will be branded a Coward for three days.",
    # Gurubashi
    "Once an hour a treasure chest appears in the Gurubashi Arena. It disappears after five minutes, so be quick.",
    "The Battlegrounds tab shows a countdown to the next Gurubashi Arena chest.",
    "Mirri Twiceborn in the Gurubashi Arena will change your character's name or race, free of charge.",
    "Dealer Rixa in the Gurubashi Arena queues you for Solo Shuffle: six rounds of 3v3, with the teams dealt again every round.",
    # Character modes and hubs
    "World characters level up and roam Azeroth. Tournament characters skip the world and fight in battlegrounds and arenas with their own gear and queues.",
    "The word above a player's name tells you whether they are a World character, a Tournament character or a Bot. You can turn it off under Interface, AddOns, Centurion.",
    "You can have up to 20 characters on Centurion.",
    "Collect Warchief's Socks and bring them to the Warchief in the Gurubashi Arena to start unlocking the Centurion hubs.",
    "Each sock quest opens a new hub with its own vendors: Gurubashi, Thrall's Stash, Blackrock and Coldarra, New Hearthglen, and the Caverns of Time.",
    "Every Portal Trainer teaches all six city teleports.",
    "Centurion's world ends at level 60. Outland and Northrend are closed.",
    "At level 60, class trainers sell extras: Ritual of Souls for warlocks, Beast Rider for hunters and Mass Thorns for druids.",
    "Beast Rider lets a hunter ride their own pet.",
    "Tauren can be Rogues on Centurion.",
    # Battlegrounds and arenas
    "Below level 60, battlegrounds reward Spoils chests holding green gear chosen for your class.",
    "Every battleground and arena is tuned to pay the same honor per hour, so play the ones you enjoy.",
    "Your cooldowns are reset when you enter a battleground or arena, and your old ones come back when you leave.",
    "Battlegrounds, arenas and duels never use up your reagents or ammo.",
    "Try Centurion's own battlegrounds, such as the Violet Hold wave defense and the Tanaris deathmatch.",
    "In the Violet Hold Gauntlet, the Boon Broker sells boons and legendary weapons that last for the rest of the run.",
    "Baradin Hold: Four Corners pits up to four squads against each other, one per corner.",
    "Dalaran Sewers and the Ring of Valor are open, along with new arenas such as the Eye of Eternity and the Gamesman's Hall.",
    # Playerbots and the world
    "Hundreds of playerbots live on Centurion. They level, quest, sell on the auction house and fight back.",
    "Until level 15, Recruit's Resolve makes you hit playerbots harder and take less damage from them.",
    "The Warchief takes the name and face of the player with the most honor this week.",
    "The body lying in each inn belongs to the player who died most often in that zone this week.",
    "Your world map shows every graveyard in the zone.",
    "Type /join World to talk with everyone on the realm.",
    "Damage absorb shields show as an overlay on player and target health bars.",
    # Playerbots
    "If a playerbot on your team is carrying a flag, whisper it the word drop and it will drop the flag for you.",
    "A playerbot killed by a player drops its gear in a cache, including weapons and armor from its bags.",
    "Outside battlegrounds and arenas, playerbots leave Tournament characters alone.",
    "With Arena bot matches checked, a skirmish with no one left to match you against is filled out with bots. Rated arenas never are.",
]


def main(ref_path, out_path):
    ref = open(ref_path, 'rb').read()
    magic, n, fields, rec_size, _ = struct.unpack('<4s4I', ref[:20])
    assert magic == b'WDBC' and fields == 18 and rec_size == 72, (magic, fields, rec_size)
    flags = struct.unpack('<I', ref[20 + 17 * 4:20 + 18 * 4])[0]  # copy the stock locale mask

    strings = bytearray(b'\0')
    records = bytearray()
    for i, tip in enumerate(TIPS, start=1):
        text = PREFIX + tip
        assert '\\' not in text and len(text) < 300, text
        off = len(strings)
        strings += text.encode('utf-8') + b'\0'
        row = [0] * 18
        row[0] = i
        row[1] = off
        row[17] = flags
        records += struct.pack('<18I', *row)

    header = struct.pack('<4s4I', b'WDBC', len(TIPS), 18, 72, len(strings))
    open(out_path, 'wb').write(header + records + strings)
    print(f'{len(TIPS)} tips, {len(strings)} string bytes, flags {flags}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
