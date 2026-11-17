# Nen

A co-op game for long-distance couples, played on your own time. Unreal Engine 5.7 with a Supabase backend.

## Backend test commands

Type these in the in-game console (`` ` `` key) while playing. Each command runs as the player signed in to that window, and signs in automatically if needed. Results show on screen and in the log under `LogNenBackend`. The commands are left out of Shipping builds.

| Command | What it does |
|---|---|---|
| `nen.CreateCouple` | Starts a couple and prints its invite code. If you already have a couple, prints its existing code instead
| `nen.InviteCode` | Prints your couple's invite code, if your partner hasn't joined yet
| `nen.Join <code>` | Joins your partner's couple with their invite code
| `nen.Pigeon <message>` | Sends your partner a letter (spaces are fine)
| `nen.Storm <LevelId> [amount]` | Raises the stream in that level on your partner's side (amount defaults to 1)
| `nen.Sync` | Applies your partner's world events, prints every stream flow, then shows new letters and marks them read
| `nen.Status` | Shows which player this window is signed in as, and its world state
| `nen.ResetLogin` | Forgets this window's saved login; the next command signs in as a new player

### Testing two players

1. In the Play settings, set **Number of Players: 2** and **Net Mode: Play Standalone**, then press Play.
2. Window A: `nen.CreateCouple`
3. Window B: `nen.Join <code from A>`
4. Window A: `nen.Storm Ravine 1`, then `nen.Pigeon hello`
5. Window B: `nen.Sync`. You should see `Ravine stream flow = 1.00` and the letter.

Each window keeps its own login between runs: the first window uses the `NenAuth` save slot, the second `NenAuth_PIE1`, and so on. For standalone or packaged builds, launch with `-NenProfile=<name>` to pick a separate slot.

If you delete test users in the Supabase dashboard, run `nen.ResetLogin` in each window that used them.

## Backend smoke test

Checks the Supabase backend on its own, without Unreal: pairing, mail, world events and the security rules.

```bash
python Tools/backend_smoke_test.py
```

It reads the URL and key from `Config/DefaultGame.ini` and needs only Python. Each run creates 3 anonymous test users; delete them under **Authentication → Users** in the Supabase dashboard.
