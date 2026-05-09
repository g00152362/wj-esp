# Locale-Specific No-Network Audio Spec

## Goal

When Wi-Fi configuration fails or times out, play a locale-specific `no_network.ogg` from `main/assets/locales/<locale>/` if present. Fall back to the existing shared asset (`Lang::Sounds::OGG_NO_NETWORK`, pointing to `main/assets/common/no_network.ogg`) otherwise. Preserve the current UI strings behavior.

## Context

- `WifiBoard::StartNetwork()` handles retry and feedback after Wi-Fi connection attempts.
- It currently calls `ShowNoNetworkPrompt()` then `display->ShowNotification(Lang::Strings::NO_NETWORK, ...)` and `application.PlaySound(Lang::Sounds::OGG_NO_NETWORK)`.
- `Lang::Sounds::OGG_NO_NETWORK` maps to the common asset packaged via linker symbols in `main/assets/lang_config.h`.
- Asset pipeline exposes localized strings through `Lang::CODE` and associated JSON data but does not currently expose locale-specific sound clips.
- Future localized audio files will be provided under `main/assets/locales/<locale>/no_network.ogg`.

## Requirements

1. Determine the active locale code (reuse the mechanism driving localized strings; `Lang::CODE` assumed).
2. Before playing the existing sound, attempt to locate `main/assets/locales/<locale>/no_network.ogg`.
3. If the locale-specific file exists, play it.
4. If not, fall back to the current behavior (play `Lang::Sounds::OGG_NO_NETWORK`).
5. Log a warning when the locale clip is missing to aid asset rollout, but do not spam logs on every retry loop (ensure at most once per boot for a missing locale).
6. Keep the retry/resume flow unchanged otherwise.
7. Local playback API should leverage existing audio service; introduce a helper if needed but avoid duplicating decode logic.
8. No changes to asset generation scripts are required now; design must tolerate absence of locale audio.

## Non-Goals

- Creating or bundling actual locale `no_network.ogg` assets.
- Overhauling the audio service or Lang config generator.
- Altering UI text prompts or retry durations.

## Approach

### Locale clip resolution

- Reuse `Lang::CODE` for locale identifier; confirm it aligns with directory names (e.g., `en-US`).
- Construct absolute path using the asset mount (e.g., `/spiffs/main/assets/locales/<locale>/no_network.ogg`; adjust prefix to match the platform's filesystem layout).
- Use `stat`/`access` (POSIX) to test presence.
- Cache result in a small static map keyed by locale to avoid repeated filesystem checks and duplicate warnings.

### Playback integration

- Extend `Application` with `PlaySoundFromFile(const char* path)` that opens the file from VFS, reads into a buffer (files are ~2 KB) and feeds it to `audio_service_.PlaySound`.
- On failure to open/read, log and return `false` so callers can fall back.

### WifiBoard modification

- Introduce helper `TryPlayLocalizedNoNetworkSound()` within `WifiBoard` that:
  1. Builds locale path (`Lang::CODE`).
  2. Calls `Application::PlaySoundFromFile`.
  3. If it returns `false`, plays `Lang::Sounds::OGG_NO_NETWORK`.
- Replace the existing `application.PlaySound(Lang::Sounds::OGG_NO_NETWORK);` call in `StartNetwork()` with the helper, ensuring behavior in the retry loop remains identical.

## Testing

- Manual validation scenarios:
  1. Without locale file: verify fallback audio plays and a single warning is logged.
  2. With locale file present: confirm localized clip plays (via logs/audio).

## Risks

- Filesystem path prefix might differ (`/spiffs`, `/littlefs`, etc.); must confirm the correct mount for packaged assets.
- Reading audio into memory adds temporary allocation; acceptable for small ogg files but monitor for larger assets.
- Logging needs to avoid spamming; implement per-locale once-only warning.

## Open Questions

- Exact filesystem root for bundled assets (assumed `/spiffs`).
- Whether `Application` already exposes a file-based playback helper (preferred to reuse if available).
- Confirm thread/context safety of accessing new helper from Wi-Fi task.
