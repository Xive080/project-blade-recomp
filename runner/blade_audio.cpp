// Salida del audio mezclado por el SPU2 del IOP emulado (ps2xIOP, spu2_output.h). Solo variables de entorno:
//   BLADE_AUDIO=0              sin audio: el SPU2 sigue avanzando (posiciones, ENDX) pero la mezcla se descarta.
//   BLADE_AUDIO_DUMP=ruta.wav  vuelca la mezcla a un WAV estereo de 16 bits a 48 kHz, sin tocar el altavoz (implica
//                              que no se abre el dispositivo). El WAV sigue el reloj del IOP: su duracion es el tiempo
//                              emulado, no el tiempo real.
// Sin ninguna de las dos, la mezcla va al dispositivo de audio de raylib con un anillo de ~0,4 s: el tiempo del IOP es
// el maestro (el productor nunca espera) y si el dispositivo consume mas de lo producido suena silencio (se rellena
// despues de un precarga de ~85 ms); si la emulacion va mas rapida que el tiempo real se descartan las muestras mas
// antiguas, para no acumular retardo.
#include "ps2x/iop/spu2_output.h"

#include "raylib.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
    constexpr size_t kRingFrames = 19200; // 0,4 s
    constexpr size_t kPrimeFrames = 4096;

    struct BladeAudio
    {
        bool enabled = true;
        std::FILE *wav = nullptr;
        uint64_t wavFrames = 0;
        uint64_t nextHeaderFrames = 0;

        std::mutex lock;
        std::vector<int16_t> ring = std::vector<int16_t>(kRingFrames * 2);
        size_t readPos = 0, count = 0; // en fotogramas
        bool primed = false;
        uint64_t underruns = 0, dropped = 0;

        bool deviceTried = false;
        bool deviceOk = false;
        AudioStream stream{};

        void writeHeader()
        {
            if (!wav)
                return;
            const uint32_t dataBytes = static_cast<uint32_t>(std::min<uint64_t>(wavFrames * 4u, 0xFFFFFFFFu - 36u));
            const long pos = std::ftell(wav);
            std::fseek(wav, 0, SEEK_SET);
            uint8_t h[44];
            auto put32 = [&](int o, uint32_t v) { for (int i = 0; i < 4; ++i) h[o + i] = static_cast<uint8_t>(v >> (8 * i)); };
            auto put16 = [&](int o, uint16_t v) { h[o] = static_cast<uint8_t>(v); h[o + 1] = static_cast<uint8_t>(v >> 8); };
            std::memcpy(h, "RIFF", 4);
            put32(4, 36u + dataBytes);
            std::memcpy(h + 8, "WAVEfmt ", 8);
            put32(16, 16);
            put16(20, 1);
            put16(22, 2);
            put32(24, 48000);
            put32(28, 48000 * 4);
            put16(32, 4);
            put16(34, 16);
            std::memcpy(h + 36, "data", 4);
            put32(40, dataBytes);
            std::fwrite(h, 1, sizeof(h), wav);
            std::fseek(wav, pos, SEEK_SET);
            std::fflush(wav);
        }

        void closeWav()
        {
            if (wav)
            {
                writeHeader();
                std::fclose(wav);
                wav = nullptr;
            }
        }

        void push(const int16_t *frames, size_t n)
        {
            std::lock_guard<std::mutex> guard(lock);
            if (n > kRingFrames)
            {
                frames += (n - kRingFrames) * 2;
                dropped += n - kRingFrames;
                n = kRingFrames;
            }
            if (count + n > kRingFrames)
            {
                const size_t drop = count + n - kRingFrames;
                readPos = (readPos + drop) % kRingFrames;
                count -= drop;
                dropped += drop;
            }
            size_t write = (readPos + count) % kRingFrames;
            for (size_t i = 0; i < n; ++i)
            {
                ring[write * 2] = frames[i * 2];
                ring[write * 2 + 1] = frames[i * 2 + 1];
                write = (write + 1) % kRingFrames;
            }
            count += n;
        }

        void pull(int16_t *out, size_t n)
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!primed && count >= kPrimeFrames)
                primed = true;
            size_t done = 0;
            if (primed)
            {
                const size_t take = std::min(n, count);
                for (size_t i = 0; i < take; ++i)
                {
                    out[i * 2] = ring[readPos * 2];
                    out[i * 2 + 1] = ring[readPos * 2 + 1];
                    readPos = (readPos + 1) % kRingFrames;
                }
                count -= take;
                done = take;
                if (take < n)
                {
                    primed = false;
                    ++underruns;
                }
            }
            if (done < n)
                std::memset(out + done * 2, 0, (n - done) * 4);
        }
    };

    BladeAudio &audio()
    {
        static BladeAudio instance;
        return instance;
    }

    void deviceCallback(void *buffer, unsigned int frames)
    {
        audio().pull(static_cast<int16_t *>(buffer), frames);
    }

    void sink(void *, const int16_t *frames, size_t n)
    {
        BladeAudio &a = audio();
        if (!a.enabled)
            return;
        if (a.wav)
        {
            std::fwrite(frames, 4, n, a.wav);
            a.wavFrames += n;
            if (a.wavFrames >= a.nextHeaderFrames)
            {
                a.writeHeader();
                a.nextHeaderFrames = a.wavFrames + 48000;
            }
            return;
        }
        if (!a.deviceTried && IsAudioDeviceReady())
        {
            a.deviceTried = true;
            SetAudioStreamBufferSizeDefault(1024);
            a.stream = LoadAudioStream(48000, 16, 2);
            a.deviceOk = IsAudioStreamValid(a.stream);
            if (a.deviceOk)
            {
                SetAudioStreamCallback(a.stream, deviceCallback);
                PlayAudioStream(a.stream);
            }
        }
        if (a.deviceOk)
            a.push(frames, n);
    }

    const bool kInstalled = []()
    {
        BladeAudio &a = audio();
        const char *off = std::getenv("BLADE_AUDIO");
        if (off && std::strcmp(off, "0") == 0)
            a.enabled = false;
        const char *dump = std::getenv("BLADE_AUDIO_DUMP");
        if (a.enabled && dump && *dump)
        {
            a.wav = std::fopen(dump, "wb");
            if (a.wav)
            {
                a.writeHeader();
                std::fseek(a.wav, 44, SEEK_SET);
                std::atexit([]() { audio().closeWav(); });
            }
            else
            {
                std::fprintf(stderr, "[blade:audio] no se puede crear %s\n", dump);
            }
        }
        ps2x::iop::setSpu2Sink(&sink, nullptr);
        return true;
    }();
}
