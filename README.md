# FlacoidCodecCorruptor

Frame-based predictor and residual corruption effect for destructive codec-style audio damage.

## Build

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DFLACOIDCODECCORRUPTOR_BUILD_PLUGIN=ON -DFLACOIDCODECCORRUPTOR_BUILD_TESTS=ON
cmake --build build/release --target ehl_stage_products FlacoidCodecCorruptorIntegrationTests --parallel 2
ctest --test-dir build/release --output-on-failure
```

Artifacts are staged under:

- `artifacts/plugin-release/macos-arm64/` on macOS
- `artifacts/plugin-release/windows-x64/` on Windows
- `artifacts/plugin-release/linux-x64/` on Linux

On local macOS builds outside CI, VST3 and AU bundles are also copied after
build to the current user's standard plug-in folders:

- `~/Library/Audio/Plug-Ins/VST3`
- `~/Library/Audio/Plug-Ins/Components`

Standalone products remain only in the build or staged artifact tree; they are
not copied under `Audio/Plug-Ins`. CI and non-macOS builds default this copying
off. Override explicitly with `-DEHL_COPY_PLUGIN_AFTER_BUILD=ON|OFF`.

## Identity

- Company: `EsionHsrahLatigid`
- Manufacturer code: `EHL_`
- Plugin code: `FlCC`
- Bundle ID: `jp.ehl.flacoidcodeccorruptor`
