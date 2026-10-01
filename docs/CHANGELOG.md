# Changelog

Player-facing changes to FFB Co-op.exe, newest first. One bullet per change, ending with the issue it
closes.

## Unreleased

- FFB Co-op.exe only trusts files signed by the FFB co-op key: a games.json or a game's manifest from coopmods.com without a valid signature is refused with a one-line warning, nothing is updated or downloaded, and the installed version starts. (#21)
- FFB Co-op.exe starts the game with co-op: put it next to the game's exe and run it. It downloads the mod into `FFB Co-op\` and starts it, passing on any arguments you gave it. Offline, it starts the version already installed with a one-line warning. Settings start fresh: nothing is copied from an existing Mewgenics co-op install. (#6)
- FFB Co-op.exe updates itself: when coopmods.com has a newer version it downloads it, checks it, replaces itself and restarts with the same arguments. A failed update is skipped with a one-line warning. (#5)
