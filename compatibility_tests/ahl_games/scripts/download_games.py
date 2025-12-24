#!/usr/bin/env python3
"""
Download David Ahl's BASIC Computer Games from GitHub

Downloads all 96 games from the coding-horror/basic-computer-games repository.
These games are used for compatibility testing but cannot be stored in our repo.

Cross-platform compatible (Windows, macOS, Linux).
"""

import os
import sys
import json
import urllib.request
import urllib.error
import ssl
import time
from pathlib import Path
from typing import Optional, Dict, List, Tuple

# Configuration
GITHUB_API_BASE = "https://api.github.com/repos/coding-horror/basic-computer-games/contents"
GITHUB_RAW_BASE = "https://raw.githubusercontent.com/coding-horror/basic-computer-games/main"

# Directory structure
SCRIPT_DIR = Path(__file__).parent.absolute()
AHL_DIR = SCRIPT_DIR.parent
GAMES_DIR = AHL_DIR / "games"
REGISTRY_FILE = AHL_DIR / "game_registry.json"

# Game directory to filename mapping (from David Ahl's book order)
# Maps GitHub directory name -> local filename
GAME_MAPPINGS = {
    "01_Acey_Ducey": "aceyducey.bas",
    "02_Amazing": "amazing.bas",
    "03_Animal": "animal.bas",
    "04_Awari": "awari.bas",
    "05_Bagels": "bagels.bas",
    "06_Banner": "banner.bas",
    "07_Basketball": "basketball.bas",
    "08_Batnum": "batnum.bas",
    "09_Battle": "battle.bas",
    "10_Blackjack": "blackjack.bas",
    "11_Bombardment": "bombardment.bas",
    "12_Bombs_Away": "bombsaway.bas",
    "13_Bounce": "bounce.bas",
    "14_Bowling": "bowling.bas",
    "15_Boxing": "boxing.bas",
    "16_Bug": "bug.bas",
    "17_Bullfight": "bullfight.bas",
    "18_Bullseye": "bullseye.bas",
    "19_Bunny": "bunny.bas",
    "20_Buzzword": "buzzword.bas",
    "21_Calendar": "calendar.bas",
    "22_Change": "change.bas",
    "23_Checkers": "checkers.bas",
    "24_Chemist": "chemist.bas",
    "25_Chief": "chief.bas",
    "26_Chomp": "chomp.bas",
    "27_Civil_War": "civilwar.bas",
    "28_Combat": "combat.bas",
    "29_Craps": "craps.bas",
    "30_Cube": "cube.bas",
    "31_Depth_Charge": "depthcharge.bas",
    "32_Diamond": "diamond.bas",
    "33_Dice": "dice.bas",
    "34_Digits": "digits.bas",
    "35_Even_Wins": "evenwins.bas",
    "36_Flip_Flop": "flipflop.bas",
    "37_Football": "football.bas",
    "38_Fur_Trader": "furtrader.bas",
    "39_Golf": "golf.bas",
    "40_Gomoko": "gomoko.bas",
    "41_Guess": "guess.bas",
    "42_Gunner": "gunner.bas",
    "43_Hammurabi": "hamurabi.bas",
    "44_Hangman": "hangman.bas",
    "45_Hello": "hello.bas",
    "46_Hexapawn": "hexapawn.bas",
    "47_Hi-Lo": "hi-lo.bas",
    "48_High_IQ": "highiq.bas",
    "49_Hockey": "hockey.bas",
    "50_Horserace": "horserace.bas",
    "51_Hurkle": "hurkle.bas",
    "52_Kinema": "kinema.bas",
    "53_King": "king.bas",
    "54_Letter": "letter.bas",
    "55_Life": "life.bas",
    "56_Life_for_Two": "lifefortwo.bas",
    "57_Literature_Quiz": "litquiz.bas",
    "58_Love": "love.bas",
    "59_Lunar_LEM_Rocket": "lunar.bas",
    "60_Mastermind": "mastermind.bas",
    "61_Math_Dice": "mathdice.bas",
    "62_Mugwump": "mugwump.bas",
    "63_Name": "name.bas",
    "64_Nicomachus": "nicomachus.bas",
    "65_Nim": "nim.bas",
    "66_Number": "number.bas",
    "67_One_Check": "onecheck.bas",
    "68_Orbit": "orbit.bas",
    "69_Pizza": "pizza.bas",
    "70_Poetry": "poetry.bas",
    "71_Poker": "poker.bas",
    "72_Queen": "queen.bas",
    "73_Reverse": "reverse.bas",
    "74_Rock_Scissors_Paper": "rockscissors.bas",
    "75_Roulette": "roulette.bas",
    "76_Russian_Roulette": "russianroulette.bas",
    "77_Salvo": "salvo.bas",
    "78_Sine_Wave": "sinewave.bas",
    "79_Slalom": "slalom.bas",
    "80_Slots": "slots.bas",
    "81_Splat": "splat.bas",
    "82_Stars": "stars.bas",
    "83_Stock_Market": "stockmarket.bas",
    "84_Super_Star_Trek": "superstartrek.bas",
    "85_Synonym": "synonym.bas",
    "86_Target": "target.bas",
    "87_3-D_Plot": "3dplot.bas",
    "88_3-D_Tic-Tac-Toe": "qubit.bas",
    "89_Tic-Tac-Toe": "tictactoe1.bas",
    "90_Tower": "tower.bas",
    "91_Train": "train.bas",
    "92_Trap": "trap.bas",
    "93_23_Matches": "23matches.bas",
    "94_War": "war.bas",
    "95_Weekday": "weekday.bas",
    "96_Word": "word.bas",
}


def create_ssl_context() -> ssl.SSLContext:
    """Create SSL context for HTTPS requests."""
    ctx = ssl.create_default_context()
    return ctx


def fetch_url(url: str, retries: int = 3, delay: float = 1.0) -> Optional[bytes]:
    """Fetch URL content with retries."""
    ctx = create_ssl_context()

    for attempt in range(retries):
        try:
            req = urllib.request.Request(
                url,
                headers={
                    'User-Agent': 'Altair-BASIC-Compatibility-Tester/1.0',
                    'Accept': 'application/vnd.github.v3+json',
                }
            )
            with urllib.request.urlopen(req, context=ctx, timeout=30) as response:
                return response.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            if e.code == 403:  # Rate limited
                print(f"  Rate limited, waiting {delay * (attempt + 1)}s...")
                time.sleep(delay * (attempt + 1))
            else:
                print(f"  HTTP error {e.code}: {e.reason}")
        except urllib.error.URLError as e:
            print(f"  Network error: {e.reason}")
        except Exception as e:
            print(f"  Error: {e}")

        if attempt < retries - 1:
            time.sleep(delay)

    return None


def find_bas_file_in_dir(github_dir: str) -> Optional[str]:
    """Find the .bas file name in a GitHub directory."""
    url = f"{GITHUB_API_BASE}/{github_dir}"
    data = fetch_url(url)

    if not data:
        return None

    try:
        contents = json.loads(data.decode('utf-8'))
        for item in contents:
            if item['type'] == 'file' and item['name'].lower().endswith('.bas'):
                return item['name']
    except (json.JSONDecodeError, KeyError):
        pass

    return None


def download_game(github_dir: str, local_filename: str, games_dir: Path) -> Tuple[bool, str]:
    """Download a single game file.

    Returns:
        Tuple of (success, message)
    """
    # Try common filename patterns directly (avoids API rate limits)
    # The .bas file is typically named after the game
    game_name = github_dir.split('_', 1)[1] if '_' in github_dir else github_dir
    possible_names = [
        local_filename,  # Our expected local name
        game_name.lower().replace('_', '').replace('-', '') + '.bas',
        game_name.lower().replace('_', '-') + '.bas',
        game_name.lower().replace('-', '_') + '.bas',
        game_name.lower() + '.bas',
    ]

    # Try each possible filename
    for bas_filename in possible_names:
        url = f"{GITHUB_RAW_BASE}/{github_dir}/{bas_filename}"
        content = fetch_url(url)
        if content:
            break
    else:
        # If none worked, try the API to find the actual filename
        bas_filename = find_bas_file_in_dir(github_dir)
        if not bas_filename:
            return False, f"No .bas file found in {github_dir}"

        url = f"{GITHUB_RAW_BASE}/{github_dir}/{bas_filename}"
        content = fetch_url(url)

        if not content:
            return False, f"Failed to download {url}"

    # Save to local file
    local_path = games_dir / local_filename
    try:
        # Decode and normalize line endings
        text = content.decode('utf-8', errors='replace')
        # Normalize to Unix line endings
        text = text.replace('\r\n', '\n').replace('\r', '\n')

        local_path.write_text(text, encoding='utf-8')
        return True, f"Downloaded {local_filename}"
    except Exception as e:
        return False, f"Failed to save {local_filename}: {e}"


def download_all_games(games_dir: Path, force: bool = False, quiet: bool = False) -> Dict[str, bool]:
    """Download all games from GitHub.

    Args:
        games_dir: Directory to save games
        force: If True, re-download even if file exists
        quiet: If True, minimal output

    Returns:
        Dict mapping filename to success status
    """
    games_dir.mkdir(parents=True, exist_ok=True)

    results = {}
    total = len(GAME_MAPPINGS)

    if not quiet:
        print(f"\nDownloading {total} games from coding-horror/basic-computer-games...")
        print(f"Target directory: {games_dir}\n")

    for i, (github_dir, local_filename) in enumerate(GAME_MAPPINGS.items(), 1):
        local_path = games_dir / local_filename

        # Skip if already exists and not forcing
        if local_path.exists() and not force:
            if not quiet:
                print(f"[{i:2}/{total}] {local_filename}: exists (skipped)")
            results[local_filename] = True
            continue

        if not quiet:
            print(f"[{i:2}/{total}] {local_filename}: downloading...", end=" ", flush=True)

        success, message = download_game(github_dir, local_filename, games_dir)
        results[local_filename] = success

        if not quiet:
            if success:
                print("OK")
            else:
                print(f"FAILED - {message}")

        # Small delay to avoid rate limiting
        time.sleep(0.1)

    # Summary
    successful = sum(1 for v in results.values() if v)
    failed = sum(1 for v in results.values() if not v)

    if not quiet:
        print(f"\nDownload complete: {successful} successful, {failed} failed")

    return results


def verify_games(games_dir: Path) -> Tuple[int, int, List[str]]:
    """Verify all games are present.

    Returns:
        Tuple of (present_count, missing_count, missing_files)
    """
    present = 0
    missing = []

    for local_filename in GAME_MAPPINGS.values():
        if (games_dir / local_filename).exists():
            present += 1
        else:
            missing.append(local_filename)

    return present, len(missing), missing


def clean_games(games_dir: Path) -> int:
    """Remove all downloaded games.

    Returns:
        Number of files removed
    """
    removed = 0

    for local_filename in GAME_MAPPINGS.values():
        path = games_dir / local_filename
        if path.exists():
            path.unlink()
            removed += 1

    return removed


def main():
    """Main entry point."""
    import argparse

    parser = argparse.ArgumentParser(
        description="Download David Ahl's BASIC Computer Games from GitHub"
    )
    parser.add_argument(
        '--force', '-f',
        action='store_true',
        help='Re-download even if files exist'
    )
    parser.add_argument(
        '--verify', '-v',
        action='store_true',
        help='Only verify games are present'
    )
    parser.add_argument(
        '--clean', '-c',
        action='store_true',
        help='Remove all downloaded games'
    )
    parser.add_argument(
        '--quiet', '-q',
        action='store_true',
        help='Minimal output'
    )
    parser.add_argument(
        '--games-dir',
        type=Path,
        default=GAMES_DIR,
        help=f'Directory for game files (default: {GAMES_DIR})'
    )

    args = parser.parse_args()

    if args.clean:
        removed = clean_games(args.games_dir)
        print(f"Removed {removed} game files")
        return 0

    if args.verify:
        present, missing_count, missing = verify_games(args.games_dir)
        print(f"Games present: {present}/{len(GAME_MAPPINGS)}")
        if missing:
            print(f"Missing games:")
            for f in missing:
                print(f"  - {f}")
        return 0 if missing_count == 0 else 1

    # Download all games
    results = download_all_games(args.games_dir, args.force, args.quiet)

    # Return non-zero if any failed
    failed = sum(1 for v in results.values() if not v)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
