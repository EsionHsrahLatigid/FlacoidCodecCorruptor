# FlacoidCodecCorruptor

Frame-based predictor and residual corruption effect for destructive codec-style audio damage.

## Build

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DFLACOIDCODECCORRUPTOR_BUILD_PLUGIN=ON -DFLACOIDCODECCORRUPTOR_BUILD_TESTS=ON
cmake --build build/release --target FlacoidCodecCorruptor_Artifacts FlacoidCodecCorruptorIntegrationTests --parallel 2
ctest --test-dir build/release --output-on-failure
```

Artifacts are staged under `artifacts/Release/`.

## Identity

- Company: `EsionHsrahLatigid`
- Manufacturer code: `EHL_`
- Plugin code: `FlCC`
- Bundle ID: `jp.ehl.flacoidcodeccorruptor`
