#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <random>

namespace {
//============================== Utilities ==============================
inline float clamp1(float x) noexcept { return juce::jlimit(-1.0f, 1.0f, x); }

// Light soft clip (cubic) — cheap and stable
inline float softClip(float x) noexcept {
  x = clamp1(x);
  // cubic soft clip
  return x - (x * x * x) * 0.3333333333f;
}

//============================== Ring Buffer ==============================
struct RingBuffer {
  void initialise(int channels, int capacitySamples) {
    ch = channels;
    cap = juce::jmax(1, capacitySamples);
    data.clear();
    data.resize((size_t)ch);
    for (auto &v : data)
      v.assign((size_t)cap, 0.0f);

    writePos = 0;
    readPos = 0;
    avail = 0;
  }

  int capacity() const noexcept { return cap; }
  int available() const noexcept { return avail; }
  int freeSpace() const noexcept { return cap - avail; }

  // Push interleaved by channel (buffer[chan][i])
  void push(const juce::AudioBuffer<float> &buffer, int numSamples) {
    const int n = juce::jmin(numSamples, freeSpace());
    if (n <= 0)
      return;

    for (int i = 0; i < n; ++i) {
      const int idx = (writePos + i) % cap;
      for (int c = 0; c < ch; ++c)
        data[(size_t)c][(size_t)idx] = buffer.getReadPointer(c)[i];
    }

    writePos = (writePos + n) % cap;
    avail += n;
  }

  // Push from raw frame arrays
  void pushFrame(const float *const *frame, int numSamples) {
    const int n = juce::jmin(numSamples, freeSpace());
    if (n <= 0)
      return;

    for (int i = 0; i < n; ++i) {
      const int idx = (writePos + i) % cap;
      for (int c = 0; c < ch; ++c)
        data[(size_t)c][(size_t)idx] = frame[c][i];
    }

    writePos = (writePos + n) % cap;
    avail += n;
  }

  // Pop into AudioBuffer
  void pop(juce::AudioBuffer<float> &buffer, int numSamples) {
    const int n = juce::jmin(numSamples, avail);

    for (int c = 0; c < buffer.getNumChannels(); ++c)
      juce::FloatVectorOperations::clear(buffer.getWritePointer(c), numSamples);

    if (n <= 0)
      return;

    for (int i = 0; i < n; ++i) {
      const int idx = (readPos + i) % cap;
      for (int c = 0; c < ch; ++c)
        buffer.getWritePointer(c)[i] = data[(size_t)c][(size_t)idx];
    }

    readPos = (readPos + n) % cap;
    avail -= n;
  }

  // Pop into raw arrays (frame[c][i])
  void popFrame(float *const *frame, int numSamples) {
    const int n = juce::jmin(numSamples, avail);

    for (int c = 0; c < ch; ++c)
      juce::FloatVectorOperations::clear(frame[c], numSamples);

    if (n <= 0)
      return;

    for (int i = 0; i < n; ++i) {
      const int idx = (readPos + i) % cap;
      for (int c = 0; c < ch; ++c)
        frame[c][i] = data[(size_t)c][(size_t)idx];
    }

    readPos = (readPos + n) % cap;
    avail -= n;
  }

  int ch{2};
  int cap{1};
  int writePos{0};
  int readPos{0};
  int avail{0};
  std::vector<std::vector<float>> data;
};

//============================== Scheduler ==============================
struct GlitchScheduler {
  enum class State { Normal, Corrupt, Recovery };

  void prepare(double sr, int frameSize) {
    sampleRate = sr;
    frameSamples = frameSize;
    state = State::Normal;
    stateFramesLeft = 0;
    phase = 0.0f;
    lastIntensity = 0.0f;
  }

  void setSeed(uint32_t s) {
    seed = s;
    rng.seed(seed);
  }

  // Call once per frame, returns "effective corruption amount" [0..1]
  float tick(float intensity, float rateHz, float durationMs,
             float resyncBias) {
    const float frameDur =
        (float)frameSamples / (float)sampleRate; // seconds per frame
    const float lambda = juce::jmax(0.0f, rateHz) * frameDur;
    const float pEvent = 1.0f - std::exp(-lambda); // Poisson arrival per frame

    // state transition
    if (state == State::Normal) {
      if (uniform() < pEvent && intensity > 0.001f) {
        state = State::Corrupt;
        stateFramesLeft =
            juce::jmax(1, (int)std::round((durationMs / 1000.0f) / frameDur));
        // snapshot peak intensity for this corruption episode
        episodePeak = intensity;
      }
    } else if (state == State::Corrupt) {
      if (--stateFramesLeft <= 0) {
        state = State::Recovery;
        // Recovery length scales with ResyncBias
        const float recSec =
            juce::jmap(juce::jlimit(0.0f, 1.0f, resyncBias), 0.05f, 0.40f);
        stateFramesLeft = juce::jmax(1, (int)std::round(recSec / frameDur));
      }
    } else // Recovery
    {
      if (--stateFramesLeft <= 0)
        state = State::Normal;
    }

    // compute effective intensity
    float eff = 0.0f;
    if (state == State::Corrupt)
      eff = episodePeak;
    else if (state == State::Recovery) {
      // exponential-ish decay
      const float t = (float)stateFramesLeft /
                      (float)juce::jmax(1, recoveryFramesEstimate());
      eff = episodePeak * t;
    }

    lastIntensity = eff;
    return eff;
  }

  bool isCorrupt() const noexcept { return state == State::Corrupt; }
  bool isRecovery() const noexcept { return state == State::Recovery; }

  float uniform() { return dist(rng); }

  // When in corruption/recovery, use this to bias drop/repeat decisions
  float corruptionAmount() const noexcept { return lastIntensity; }

private:
  int recoveryFramesEstimate() const noexcept {
    // rough estimate; keeps decay consistent
    return juce::jmax(1, stateFramesLeft + 1);
  }

  double sampleRate{48000.0};
  int frameSamples{4096};

  State state{State::Normal};
  int stateFramesLeft{0};

  uint32_t seed{1};
  std::mt19937 rng{1};
  std::uniform_real_distribution<float> dist{0.0f, 1.0f};

  float episodePeak{0.0f};
  float lastIntensity{0.0f};
  float phase{0.0f};
};

//============================== Predictor ==============================
// Lightweight LPC-like predictor with mutable coefficients.
// This is intentionally "codec-ish" but not heavy: per-frame coefficient
// updates are simple.
//============================== Predictor ==============================
struct Predictor {
  void prepare(int orderIn) {
    setOrder(orderIn);

    // Initial predictor coefficients.
    for (int k = 0; k < order; ++k)
      coeff[(size_t)k] = 0.6f * std::pow(0.5f, (float)k);

    resetHistory();
  }

  void setOrder(int orderIn) {
    order = juce::jlimit(1, 16, orderIn);
    coeff.assign((size_t)order, 0.0f);
    hist.assign((size_t)order, 0.0f);
  }

  void setCoeffs(const std::vector<float> &c) {
    const int n = juce::jmin((int)c.size(), order);
    for (int i = 0; i < n; ++i)
      coeff[(size_t)i] = c[(size_t)i];
    for (int i = n; i < order; ++i)
      coeff[(size_t)i] = 0.0f;
  }

  void resetHistory() { std::fill(hist.begin(), hist.end(), 0.0f); }

  float predict(float xPrev) noexcept {
    for (int k = order - 1; k > 0; --k)
      hist[(size_t)k] = hist[(size_t)(k - 1)];
    hist[0] = xPrev;

    float p = 0.0f;
    for (int k = 0; k < order; ++k)
      p += coeff[(size_t)k] * hist[(size_t)k];

    return p;
  }

  void applyChaos(std::mt19937 &rng, float chaosAmount) {
    if (chaosAmount <= 0.0f)
      return;

    std::uniform_real_distribution<float> u(-1.0f, 1.0f);

    const float step = 0.15f * chaosAmount;
    for (int k = 0; k < order; ++k)
      coeff[(size_t)k] += step * u(rng) * std::pow(0.7f, (float)k);

    float energy = 0.0f;
    for (int k = 0; k < order; ++k)
      energy += coeff[(size_t)k] * coeff[(size_t)k];

    const float maxEnergy = 2.0f;
    if (energy > maxEnergy) {
      const float g = std::sqrt(maxEnergy / energy);
      for (int k = 0; k < order; ++k)
        coeff[(size_t)k] *= g;
    }
  }

  int order{6};
  std::vector<float> coeff;
  std::vector<float> hist;
};

//============================== Residual operations
//==============================
struct ResidualOps {
  void prepare(double sr) { sampleRate = sr; }

  void apply(float *r, int n, std::mt19937 &rng, float amount,
             float clickiness) {
    if (amount <= 0.0f)
      return;

    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::uniform_real_distribution<float> uS(-1.0f, 1.0f);

    // (1) Burst spikes: "click / crackle" (dominant at higher clickiness)
    const float spikeProb = juce::jlimit(
        0.0f, 1.0f, (0.05f + 0.35f * amount) * (0.25f + 0.75f * clickiness));
    if (u01(rng) < spikeProb) {
      const int start = (int)std::floor(u01(rng) * (float)juce::jmax(1, n - 1));
      const float burstMs =
          juce::jmap(u01(rng), 0.5f, 25.0f) * (0.25f + 1.25f * amount);
      const int len = juce::jlimit(
          1, n - start, (int)std::round(burstMs * 0.001f * (float)sampleRate));

      const float A = (0.05f + 0.9f * amount) * (0.2f + 0.8f * clickiness);
      float env = 1.0f;
      const float decay = std::pow(
          0.001f, 1.0f / (float)juce::jmax(1, len)); // fast decay to ~-60dB

      for (int i = 0; i < len; ++i) {
        r[start + i] += A * env * uS(rng);
        env *= decay;
      }
    }

    // (2) Residual quantization (codec-ish "resolution collapse")
    const float qProb = juce::jlimit(0.0f, 1.0f, 0.08f + 0.35f * amount);
    if (u01(rng) < qProb) {
      const int bits =
          (int)std::round(juce::jmap(amount, 14.0f, 5.0f)); // 14 -> 5 bits
      const int levels = 1 << juce::jlimit(3, 15, bits);
      const float s = 0.5f * (levels - 1);

      for (int i = 0; i < n; ++i)
        r[i] = std::round(r[i] * s) / s;
    }

    // (3) Segment shuffle (breaks continuity)
    const float shProb =
        juce::jlimit(0.0f, 1.0f, 0.04f + 0.25f * amount * (1.0f - clickiness));
    if (u01(rng) < shProb && n >= 64) {
      const int segLen = juce::jlimit(
          16, n / 2, (int)std::round(juce::jmap(amount, 24.0f, 160.0f)));
      const int a = (int)std::floor(u01(rng) * (float)(n - segLen));
      const int b = (int)std::floor(u01(rng) * (float)(n - segLen));

      for (int i = 0; i < segLen; ++i)
        std::swap(r[a + i], r[b + i]);
    }
  }

  double sampleRate{48000.0};
};

//============================== Frame Router ==============================
struct FrameRouter {
  void prepare(int channels, int frameSize, int xfadeSamples) {
    ch = channels;
    N = frameSize;
    xfade = juce::jlimit(0, frameSize / 4, xfadeSamples);

    last.assign((size_t)ch, std::vector<float>((size_t)N, 0.0f));
    stash.assign((size_t)ch, std::vector<float>((size_t)N, 0.0f));
    fragmentScratch.assign((size_t)N, 0.0f);
    shiftScratch.assign((size_t)N, 0.0f);
    hasLast = false;
  }

  // Takes processed frame "inOut[ch][i]" and may introduce *localized*
  // decode-like corruption.
  void route(float *const *inOut, std::mt19937 &rng, float amount,
             float resyncBias, float stereoDamage) {
    if (N <= 0)
      return;

    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    // Save current frame for splicing operations
    for (int c = 0; c < ch; ++c)
      std::copy(inOut[c], inOut[c] + N, stash[(size_t)c].data());

    if (!hasLast) {
      for (int c = 0; c < ch; ++c)
        std::copy(inOut[c], inOut[c] + N, last[(size_t)c].data());
      hasLast = true;
      return;
    }

    const float amt = juce::jlimit(0.0f, 1.0f, amount);

    // Instead of "drop=repeat whole frame", do localized window corruption.
    const float pWindowGlitch = juce::jlimit(
        0.0f, 0.95f, 0.10f + 0.55f * amt * (0.4f + 0.6f * (1.0f - resyncBias)));
    const float pSplice = juce::jlimit(0.0f, 0.95f, 0.05f + 0.35f * amt);
    const float pMicroRepeat = juce::jlimit(0.0f, 0.95f, 0.04f + 0.25f * amt);

    // (A) Localized window replacement from previous frame (decoder "wrong
    // residual block" feel)
    if (u01(rng) < pWindowGlitch) {
      const int maxWin = juce::jlimit(
          64, N / 2, (int)std::round(juce::jmap(amt, 96.0f, 1600.0f)));
      const int win =
          juce::jlimit(64, N - 1, (int)std::round(u01(rng) * (float)maxWin));
      const int start = juce::jlimit(
          0, N - win, (int)std::round(u01(rng) * (float)(N - win)));

      for (int c = 0; c < ch; ++c)
        replaceWindowWithXfade(inOut[c], stash[(size_t)c].data(),
                               last[(size_t)c].data(), start, win, xfade);

      // optional tiny channel skew / offset under stereo damage
      if (ch >= 2 && stereoDamage > 0.01f && u01(rng) < 0.35f * stereoDamage) {
        const int shift =
            (int)std::round(juce::jmap(u01(rng), -12.0f, 12.0f) * stereoDamage);
        if (shift != 0)
          shiftWindowInPlace(inOut[1], start, win, shift);
      }
    }

    // (B) Micro-repeat a tiny fragment inside the frame (buffer stutter like
    // "resync search")
    if (u01(rng) < pMicroRepeat) {
      const int fragMax = juce::jlimit(
          32, 512, (int)std::round(juce::jmap(amt, 48.0f, 480.0f)));
      const int fragLen =
          juce::jlimit(16, N / 3, (int)std::round(u01(rng) * (float)fragMax));
      const int start = juce::jlimit(
          0, N - fragLen - 1, (int)std::round(u01(rng) * (float)(N - fragLen)));

      // repeat 2-5 times depending on amount
      const int reps = juce::jlimit(2, 6, 2 + (int)std::round(amt * 3.0f));

      for (int c = 0; c < ch; ++c)
        microRepeatWithXfade(inOut[c], N, start, fragLen, reps, xfade);
    }

    // (C) Splice with random seam (shorter than "half/half")
    if (u01(rng) < pSplice) {
      const int seam =
          juce::jlimit(64, N - 64, (int)std::round(u01(rng) * (float)N));
      const int seamW = juce::jlimit(
          16, 256, (int)std::round(juce::jmap(amt, 24.0f, 200.0f)));

      for (int c = 0; c < ch; ++c) {
        // inOut currently based on stash; we overwrite a region around seam
        // with previous
        const int start = juce::jlimit(0, N - seamW, seam - seamW / 2);
        const int win = seamW;
        replaceWindowWithXfade(inOut[c], stash[(size_t)c].data(),
                               last[(size_t)c].data(), start, win, xfade);
      }
    }

    // Update last frame = final output of this frame (post-routing)
    for (int c = 0; c < ch; ++c)
      std::copy(inOut[c], inOut[c] + N, last[(size_t)c].data());
    hasLast = true;
  }

private:
  static void replaceWindowWithXfade(float *dst, const float *base,
                                     const float *replace, int start, int win,
                                     int xfade) {
    // dst already holds "base" typically; enforce base then replace window
    std::copy(base, base + (start), dst);
    std::copy(base + (start + win), base + (start + win) + (0),
              dst + (start + win)); // no-op but keeps intent clear

    // copy entire base first (safe)
    // (callers often already have dst=base; keep idempotent)
    // We'll only patch the window with crossfades:
    const int end = start + win;

    // body
    for (int i = start; i < end; ++i)
      dst[i] = replace[i];

    // xfade at start edge
    const int xs = juce::jlimit(0, win / 2, xfade);
    for (int i = 0; i < xs; ++i) {
      const float t = (float)i / (float)juce::jmax(1, xs); // 0..1
      const int idx = start + i;
      dst[idx] = (1.0f - t) * base[idx] + t * replace[idx];
    }

    // xfade at end edge
    const int xe = juce::jlimit(0, win / 2, xfade);
    for (int i = 0; i < xe; ++i) {
      const float t = (float)i / (float)juce::jmax(1, xe); // 0..1
      const int idx = end - 1 - i;
      dst[idx] = (1.0f - t) * base[idx] + t * replace[idx];
    }
  }

  void microRepeatWithXfade(float *x, int totalSamples, int start,
                            int len, int reps, int xfade) {
    if (len <= 1 || reps <= 1)
      return;

    if (totalSamples <= 0 || start < 0 || totalSamples <= start)
      return;

    const int maxAvail = totalSamples - start;
    const int total = juce::jmin(juce::jmin(len * reps, 2048), maxAvail);
    if (total <= 0)
      return;

    jassert((size_t)len <= fragmentScratch.size());
    auto* frag = fragmentScratch.data();
    for (int i = 0; i < len; ++i)
      frag[i] = x[start + i];

    int write = start;
    for (int r = 0; r < reps; ++r) {
      for (int i = 0; i < len; ++i) {
        const int idx = write + i;
        if (idx < 0 || totalSamples <= idx)
          continue;
        x[idx] = frag[i];
      }

      // small crossfade at repeat boundary
      const int xs = juce::jlimit(0, len / 2, xfade);
      if (xs > 0 && r > 0) {
        for (int i = 0; i < xs; ++i) {
          const float t = (float)i / (float)xs;
          const int idx = write + i;
          if (idx < 0 || totalSamples <= idx)
            continue;
          // blend with previous sample already there (gives "decoder edge")
          x[idx] = (1.0f - t) * x[idx] + t * frag[i];
        }
      }

      write += len;
      if (write + len >= start + total)
        break;
    }
  }

  void shiftWindowInPlace(float *x, int start, int win, int shift) {
    if (shift == 0 || win <= 1)
      return;

    jassert((size_t)win <= shiftScratch.size());
    auto* tmp = shiftScratch.data();
    for (int i = 0; i < win; ++i) {
      const int src = i - shift;
      tmp[i] = (src >= 0 && src < win) ? x[start + src] : 0.0f;
    }
    for (int i = 0; i < win; ++i)
      x[start + i] = tmp[i];
  }

  int ch{2};
  int N{4096};
  int xfade{64};

  bool hasLast{false};
  std::vector<std::vector<float>> last;
  std::vector<std::vector<float>> stash;
  std::vector<float> fragmentScratch;
  std::vector<float> shiftScratch;
};

//============================== Frame Processor ==============================
struct FrameProcessor {
  // Very small LPC via autocorrelation + Levinson-Durbin
  // Returns coeffs a[0..order-1] such that p[n] = sum a[k]*x[n-1-k]
  void computeLpc(const float *xIn, int N, int order,
                  std::vector<float> &outA) {
    std::fill(outA.begin(), outA.begin() + order, 0.0f);

    // autocorr r[0..order]
    for (int k = 0; k <= order; ++k) {
      double sum = 0.0;
      for (int n = k; n < N; ++n)
        sum += (double)xIn[n] * (double)xIn[n - k];
      autocorr[(size_t)k] = (float)sum;
    }

    if (autocorr[0] < 1.0e-10f)
      return;

    // Levinson-Durbin
    std::fill(aTmp.begin(), aTmp.end(), 0.0f);
    std::fill(E.begin(), E.end(), 0.0f);
    E[0] = autocorr[0];

    for (int i = 0; i < order; ++i) {
      float acc = autocorr[(size_t)i + 1];
      for (int j = 0; j < i; ++j)
        acc += aTmp[(size_t)j] * autocorr[(size_t)(i - j)];

      float kappa = -acc / (E[(size_t)i] + 1.0e-12f);

      // update a
      std::copy(aTmp.begin(), aTmp.begin() + order, aPrev.begin());
      aTmp[(size_t)i] = kappa;
      for (int j = 0; j < i; ++j)
        aTmp[(size_t)j] = aPrev[(size_t)j] + kappa * aPrev[(size_t)(i - 1 - j)];

      E[(size_t)i + 1] = E[(size_t)i] * (1.0f - kappa * kappa);
      if (E[(size_t)i + 1] < 1.0e-12f)
        E[(size_t)i + 1] = 1.0e-12f;
    }

    std::copy(aTmp.begin(), aTmp.begin() + order, outA.begin());
  }

  void prepare(double sr, int channels, int frameSize) {
    sampleRate = sr;
    ch = channels;
    N = frameSize;

    scheduler.prepare(sr, frameSize);
    residualOps.prepare(sr);

    // Predictors per channel (or per M/S channel)
    lpcOrder = 10;
    pred[0].prepare(lpcOrder);
    pred[1].prepare(lpcOrder);

    // work buffers for LPC
    lpcCoeffTmp.assign((size_t)lpcOrder, 0.0f);
    autocorr.assign((size_t)(lpcOrder + 1), 0.0f);
    E.assign((size_t)(lpcOrder + 1), 0.0f);
    aTmp.assign((size_t)lpcOrder, 0.0f);
    aPrev.assign((size_t)lpcOrder, 0.0f);

    frameCounter = 0;
    lpcUpdateInterval = 8;

    // temp buffers
    x.assign((size_t)ch, std::vector<float>((size_t)N, 0.0f));
    y.assign((size_t)ch, std::vector<float>((size_t)N, 0.0f));
    r.assign((size_t)ch, std::vector<float>((size_t)N, 0.0f));

    frameRouter.prepare(channels, frameSize, 64);

  }

  void setSeed(uint32_t s) {
    scheduler.setSeed(s);
    seed = s;
    rng.seed(seed);
  }

  void processFrame(float *const *frameInOut, float intensity, float rateHz,
                    float durationMs, float resyncBias, float stereoDamage,
                    bool msMode, float wet) {
    wet = juce::jlimit(0.0f, 1.0f, wet);
    intensity = juce::jlimit(0.0f, 1.0f, intensity);
    rateHz = juce::jmax(0.0f, rateHz);
    durationMs = juce::jmax(1.0f, durationMs);
    resyncBias = juce::jlimit(0.0f, 1.0f, resyncBias);
    stereoDamage = juce::jlimit(0.0f, 1.0f, stereoDamage);

    // copy input to x
    for (int c = 0; c < ch; ++c)
      std::copy(frameInOut[c], frameInOut[c] + N, x[(size_t)c].data());

    // Optional M/S transform for "codec-ish" stereo interaction
    if (msMode && ch >= 2) {
      for (int i = 0; i < N; ++i) {
        const float L = x[0][(size_t)i];
        const float R = x[1][(size_t)i];
        x[0][(size_t)i] = 0.5f * (L + R); // M
        x[1][(size_t)i] = 0.5f * (L - R); // S
      }
    }

    // Tick scheduler once per frame (decides corruption state)
    const float eff = scheduler.tick(intensity, rateHz, durationMs, resyncBias);

    // Low-frequency LPC update (codec-ish stability + occasional mismatch when
    // corrupted)
    // Update every lpcUpdateInterval frames, and sometimes "miss" updates under
    // corruption
    ++frameCounter;
    const bool wantUpdate = (frameCounter % lpcUpdateInterval) == 0;

    if (wantUpdate) {
      // Under corruption, sometimes keep old coeffs (creates "stale predictor"
      // mismatch)
      const float missProb =
          scheduler.isCorrupt() ? (0.25f + 0.5f * eff) : 0.05f;
      std::uniform_real_distribution<float> u01(0.0f, 1.0f);

      if (u01(rng) >= missProb) {
        // update ch0
        computeLpc(x[0].data(), N, lpcOrder, lpcCoeffTmp);
        pred[0].setCoeffs(lpcCoeffTmp);

        if (ch >= 2) {
          computeLpc(x[1].data(), N, lpcOrder, lpcCoeffTmp);
          pred[1].setCoeffs(lpcCoeffTmp);
        }
      }
    }

    // Decide per-frame chaos parameters
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);

    // Predictor chaos — stronger when corrupt
    const float predChaosBase = eff * (0.35f + 0.65f * (1.0f - resyncBias));
    const float clickiness = juce::jlimit(0.0f, 1.0f, 0.25f + 0.75f * eff);

    // Channel skew under stereoDamage (L/R or M/S non-synchronous)
    float chSkew[2]{1.0f, 1.0f};
    if (ch >= 2 && stereoDamage > 0.0f) {
      const float delta = (u01(rng) * 2.0f - 1.0f) * 0.6f * stereoDamage;
      chSkew[0] = juce::jlimit(0.2f, 1.8f, 1.0f + delta);
      chSkew[1] = juce::jlimit(0.2f, 1.8f, 1.0f - delta);
    }

    // Reset predictor history at resync moments (gives "decoder re-lock")
    if (scheduler.isRecovery() && u01(rng) < (0.15f + 0.65f * resyncBias)) {
      pred[0].resetHistory();
      if (ch >= 2)
        pred[1].resetHistory();
    }

    // Apply coefficient chaos once per frame (not per sample)
    pred[0].applyChaos(rng, predChaosBase * chSkew[0]);
    if (ch >= 2)
      pred[1].applyChaos(rng, predChaosBase * chSkew[1]);

    // Encode-ish: compute residual and then corrupt residual
    for (int c = 0; c < ch; ++c) {
      Predictor &P = pred[juce::jmin(1, c)];
      float xPrev = 0.0f;

      for (int i = 0; i < N; ++i) {
        const float p = P.predict(xPrev);
        const float xi = x[(size_t)c][(size_t)i];
        r[(size_t)c][(size_t)i] = xi - p;
        xPrev = xi;
      }

      residualOps.apply(r[(size_t)c].data(), N, rng,
                        eff * chSkew[juce::jmin(1, c)], clickiness);
    }

    // Decode-ish: reconstruct using current predictor state + corrupted
    // residual
    for (int c = 0; c < ch; ++c) {
      Predictor &P = pred[juce::jmin(1, c)];
      float yPrev = 0.0f;

      for (int i = 0; i < N; ++i) {
        const float p = P.predict(yPrev);
        float yi = p + r[(size_t)c][(size_t)i];

        // occasional "scale fault" (codec math slip) under high eff
        if (eff > 0.25f && u01(rng) < 0.02f * eff)
          yi *= juce::jmap(u01(rng), 0.0f, 1.0f, 0.0f, 2.0f);

        y[(size_t)c][(size_t)i] = yi;
        yPrev = yi;
      }
    }

    // Frame routing: drop/repeat/splice (resync feel)
    {
      float *ioPtrs[2]{y[0].data(), (ch >= 2 ? y[1].data() : nullptr)};
      frameRouter.route(ioPtrs, rng, eff, resyncBias, stereoDamage);
    }

    // Optional M/S inverse with "matrix error" under stereoDamage
    if (msMode && ch >= 2) {
      const float k =
          1.0f + (scheduler.isCorrupt()
                      ? (0.35f * stereoDamage * (u01(rng) * 2.0f - 1.0f))
                      : 0.0f);

      for (int i = 0; i < N; ++i) {
        const float M = y[0][(size_t)i];
        const float S = y[1][(size_t)i];
        y[0][(size_t)i] = M + k * S; // L
        y[1][(size_t)i] = M - k * S; // R
      }
    }

    // Wet/Dry + Safety
    double inEnergy = 0.0;
    double outEnergy = 0.0;

    for (int c = 0; c < ch; ++c) {
      for (int i = 0; i < N; ++i) {
        const float dry = frameInOut[c][i];
        float out = juce::jmap(wet, dry, y[(size_t)c][(size_t)i]);

        out = softClip(out);
        if (!std::isfinite(out))
          out = 0.0f;

        inEnergy += (double)dry * (double)dry;
        outEnergy += (double)out * (double)out;

        frameInOut[c][i] = out;
      }
    }

    const double norm = (double)juce::jmax(1, N * ch);
    const double inRms = std::sqrt(inEnergy / norm);
    const double outRms = std::sqrt(outEnergy / norm);
    if (1.0e-5 < inRms && outRms < 1.0e-6) {
      for (int c = 0; c < ch; ++c) {
        for (int i = 0; i < N; ++i)
          frameInOut[c][i] = x[(size_t)c][(size_t)i];
      }
    }
  }

  double sampleRate{48000.0};
  int ch{2};
  int N{4096};

  uint32_t seed{1};
  std::mt19937 rng{1};

  GlitchScheduler scheduler;
  Predictor pred[2];
  ResidualOps residualOps;
  FrameRouter frameRouter;

  std::vector<std::vector<float>> x, y, r;
  int lpcOrder{10};         // 8〜12推奨（より“デコード崩壊”寄り）
  int lpcUpdateInterval{8}; // 低頻度更新（4〜16あたりが実用）
  int frameCounter{0};

  std::vector<float> lpcCoeffTmp;
  std::vector<float> autocorr;
  std::vector<float> E;
  std::vector<float> aTmp;
  std::vector<float> aPrev;
};
} // namespace

struct CodecCorruptorAudioProcessor::InstanceState {
  RingBuffer inRB, outRB;
  FrameProcessor fp;
  juce::AudioBuffer<float> frameBuf;
  juce::AudioBuffer<float> outFrameBuf;
  int frameSize{4096};
  int channels{2};
  int reinitCounter{0};
  int reinitStreak{0};
  int lastBufferChannels{0};
  int frameProcessCounter{0};

  void initialise(double sampleRate, int channelsIn, int frameSizeIn) {
    constexpr int ringSeconds = 2;

    if (sampleRate < 1.0)
      sampleRate = 44100.0;

    const int ch = juce::jmax(1, channelsIn);
    frameSize = juce::jlimit(64, 8192, frameSizeIn);
    const int cap = juce::jmax(frameSize * 2,
                               (int)std::ceil(sampleRate * ringSeconds));

    channels = ch;

    inRB.initialise(ch, cap);
    outRB.initialise(ch, cap);

    fp.prepare(sampleRate, ch, frameSize);

    frameBuf.setSize(ch, frameSize);
    outFrameBuf.setSize(ch, frameSize);

    juce::ignoreUnused(sampleRate);
  }
};

CodecCorruptorAudioProcessor::CodecCorruptorAudioProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout()) {}

CodecCorruptorAudioProcessor::~CodecCorruptorAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout
CodecCorruptorAudioProcessor::createParameterLayout() {
  using APF = juce::AudioParameterFloat;
  using API = juce::AudioParameterInt;
  using APB = juce::AudioParameterBool;

  juce::AudioProcessorValueTreeState::ParameterLayout layout;

  layout.add(std::make_unique<APF>(
      "intensity", "Intensity",
      juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));

  layout.add(std::make_unique<APF>(
      "rate", "Rate",
      juce::NormalisableRange<float>(0.05f, 20.0f, 0.001f, 0.5f),
      2.0f)); // skewed

  layout.add(std::make_unique<APF>(
      "duration", "Duration",
      juce::NormalisableRange<float>(20.0f, 2000.0f, 0.1f, 0.5f),
      250.0f)); // ms

  layout.add(std::make_unique<APF>(
      "resync", "Resync", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
      0.55f));

  layout.add(std::make_unique<APF>(
      "stereo", "StereoDamage",
      juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));

  layout.add(std::make_unique<APB>("ms", "MS Mode", true));

  layout.add(std::make_unique<APF>(
      "wet", "Wet", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

  layout.add(std::make_unique<API>("seed", "Seed", 1, 999999, 1));

  return layout;
}

bool CodecCorruptorAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  // Support mono/stereo in and out; keep in/out channel counts matched.
  const auto in = layouts.getMainInputChannelSet();
  const auto out = layouts.getMainOutputChannelSet();

  if (in != out)
    return false;
  if (in != juce::AudioChannelSet::mono() &&
      in != juce::AudioChannelSet::stereo())
    return false;

  return true;
}

//==============================================================================
void CodecCorruptorAudioProcessor::prepareToPlay(double sampleRate,
                                                 int samplesPerBlock) {
  juce::ignoreUnused(samplesPerBlock);

  const int frameSize = juce::jlimit(64, 8192, samplesPerBlock);

  if (sampleRate < 1.0) {
    const double fallback = getSampleRate();
    sampleRate = (fallback >= 1.0 ? fallback : 44100.0);
  }

  const int ch = juce::jmax(
      1, juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels()));

  // Set plugin latency to frame size (makes offline renders and alignment
  // correct)
  setLatencySamples(frameSize);

  instanceState = std::make_unique<InstanceState>();
  auto &st = *instanceState;
  st.initialise(sampleRate, ch, frameSize);
  st.fp.setSeed((uint32_t)*apvts.getRawParameterValue("seed"));
}

void CodecCorruptorAudioProcessor::releaseResources() { instanceState.reset(); }

void CodecCorruptorAudioProcessor::processBlock(
    juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi) {
  juce::ignoreUnused(midi);

  const int inputChannels = getTotalNumInputChannels();
  const int n = buffer.getNumSamples();

  if (instanceState == nullptr)
    return;

  auto &st = *instanceState;
  const int bufferChannels = buffer.getNumChannels();
  if (bufferChannels < 1)
    return;

  int actualChannels = bufferChannels;
  float *const *writePtrs = buffer.getArrayOfWritePointers();
  if (bufferChannels >= 2 && (writePtrs == nullptr || writePtrs[1] == nullptr))
    actualChannels = 1;

  // Read params (no smoothing in this minimal version; add SmoothedValue if
  // needed)
  const float intensity = apvts.getRawParameterValue("intensity")->load();
  const float rateHz = apvts.getRawParameterValue("rate")->load();
  const float duration = apvts.getRawParameterValue("duration")->load();
  const float resync = apvts.getRawParameterValue("resync")->load();
  const float stereo = apvts.getRawParameterValue("stereo")->load();
  const bool msMode = apvts.getRawParameterValue("ms")->load() > 0.5f;
  const float wet = apvts.getRawParameterValue("wet")->load();
  const int seed = (int)apvts.getRawParameterValue("seed")->load();

  if (actualChannels != st.lastBufferChannels) {
    st.lastBufferChannels = actualChannels;
    st.reinitStreak = 0;
  }

  // Ensure buffer channel count consistency
  for (int c = inputChannels; c < bufferChannels; ++c)
    buffer.clear(c, 0, n);

  if (actualChannels != st.channels)
    return;

  if (wet <= 0.0001f)
    return;

  // Update seed if changed (cheap check)
  if (seed != lastSeed) {
    st.fp.setSeed((uint32_t)seed);
    lastSeed = seed;
  }

  if (st.frameSize == n) {
    float *ioPtrs[2]{buffer.getWritePointer(0),
                     (st.channels >= 2 ? buffer.getWritePointer(1) : nullptr)};
    st.fp.processFrame(ioPtrs, intensity, rateHz, duration, resync, stereo,
                       msMode && (st.channels >= 2), wet);
    return;
  }

  // Push input to inRB
  st.inRB.push(buffer, n);

  // Process frames while available
  while (st.inRB.available() >= st.frameSize &&
         st.outRB.freeSpace() >= st.frameSize) {
    ++st.frameProcessCounter;
    // Pop one frame into frameBuf
    float *framePtrs[2]{st.frameBuf.getWritePointer(0),
                        (st.channels >= 2 ? st.frameBuf.getWritePointer(1)
                                          : nullptr)};
    st.inRB.popFrame(framePtrs, st.frameSize);

    // Copy frameBuf into outFrameBuf for in-place processing
    for (int c = 0; c < st.channels; ++c)
      st.outFrameBuf.copyFrom(c, 0, st.frameBuf, c, 0, st.frameSize);

    float *outPtrs[2]{st.outFrameBuf.getWritePointer(0),
                      (st.channels >= 2 ? st.outFrameBuf.getWritePointer(1)
                                        : nullptr)};

    st.fp.processFrame(outPtrs, intensity, rateHz, duration, resync, stereo,
                       msMode, wet);

    // Push processed frame to outRB
    const float *pushPtrsConst[2]{
        st.outFrameBuf.getReadPointer(0),
        (st.channels >= 2 ? st.outFrameBuf.getReadPointer(1) : nullptr)};
    st.outRB.pushFrame(pushPtrsConst, st.frameSize);
  }

  // Pop output for this block
  if (st.outRB.available() < n)
    return;

  st.outRB.pop(buffer, n);
}

//==============================================================================
void CodecCorruptorAudioProcessor::getStateInformation(
    juce::MemoryBlock &destData) {
  juce::MemoryOutputStream mos(destData, true);
  apvts.copyState().writeToStream(mos);
}

void CodecCorruptorAudioProcessor::setStateInformation(const void *data,
                                                       int sizeInBytes) {
  auto tree = juce::ValueTree::readFromData(data, (size_t)sizeInBytes);
  if (tree.isValid())
    apvts.replaceState(tree);
}

//==============================================================================
juce::AudioProcessorEditor *CodecCorruptorAudioProcessor::createEditor() {
  return new CodecCorruptorAudioProcessorEditor(*this);
}

//==============================================================================
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new CodecCorruptorAudioProcessor();
}
