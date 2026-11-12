#include "codec.h"
#include <wavpack.h>
#include <shlwapi.h>
#include <cmath>
namespace ttp::wv {
struct Decoder::Impl {
    Input* input{};
    std::unique_ptr<Input> correction;
    WavpackContext* context{};
    HRESULT error{S_OK};
    std::vector<int32_t> unpacked;
    ~Impl() { if (context) WavpackCloseFile(context); }
    struct Stream { Impl* owner{}; Input* input{}; uint64_t pos{}, base{}; } main, extra;
    template<class F> static int64_t call(void* id, F f, int64_t failure = -1) noexcept {
        auto& s = *static_cast<Stream*>(id); int64_t result{};
        auto hr = protect([&] { result = f(s); return S_OK; });
        if (FAILED(hr)) { if (SUCCEEDED(s.owner->error)) s.owner->error = hr; return failure; }
        return result;
    }
    static int32_t read(void* id, void* data, int32_t n) {
        return int32_t(call(id, [&](Stream& s) {
            require(n >= 0, E_INVALIDARG); ULONG done{};
            while (done < ULONG(n)) {
                require(WaitForSingleObject(s.owner->input->event, 0) != WAIT_OBJECT_0, E_ABORT);
                auto got = s.input->read(static_cast<BYTE*>(data) + done, ULONG(n) - done);
                done += got; s.pos += got; if (!got) break;
            }
            return int64_t(done);
        }, 0));
    }
    static int64_t pos(void* id) { return int64_t(static_cast<Stream*>(id)->pos); }
    static int at(void* id, int64_t offset) {
        return int(call(id, [&](Stream& s) { require(offset >= 0 && uint64_t(offset) <= s.input->size - s.base);
            s.input->at(s.base + uint64_t(offset)); s.pos = uint64_t(offset); return 0; }));
    }
    static int relative(void* id, int64_t offset, int origin) {
        return int(call(id, [&](Stream& s) {
            require(origin >= SEEK_SET && origin <= SEEK_END, E_INVALIDARG);
            const auto base = origin == SEEK_CUR ? s.pos : origin == SEEK_END ? s.input->size - s.base : 0;
            require(base <= INT64_MAX && (offset >= 0 ? uint64_t(offset) <= INT64_MAX - base : uint64_t(-(offset + 1)) + 1 <= base));
            return at(id, int64_t(base) + offset);
        }));
    }
    static int push(void* id, int c) { auto& s = *static_cast<Stream*>(id); return s.pos && !at(id, int64_t(s.pos - 1)) ? c : EOF; }
    static int64_t length(void* id) { auto& s = *static_cast<Stream*>(id); return int64_t(s.input->size - s.base); }
    static int seekable(void*) { return 1; }
};
Decoder::Decoder() = default;
Decoder::~Decoder() = default;
void Decoder::open(Input& in) {
    impl_ = std::make_unique<Impl>(); auto& s = *impl_; s.input = &in;
    in.at(0); in.signature("wvpk"); s.main = {&s, &in, 0, in.base};
    // Local companion correction streams are optional; never guess a disk path
    // for archive/remote streams. WavPack reports the actual lossy/lossless mode.
    if (!in.remote && !in.path.empty() && in.path.find(L"::") == std::wstring::npos) {
        ComPtr<IStream> stream; const auto path = in.path + L"c";
        if (SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, stream.put()))) {
            s.correction = std::make_unique<Input>(); s.correction->open(stream.p);
            s.correction->at(0); s.correction->signature("wvpk");
            s.extra = {&s, s.correction.get(), 0, s.correction->base};
        }
    }
    static WavpackStreamReader64 callbacks{Impl::read, nullptr, Impl::pos, Impl::at, Impl::relative,
        Impl::push, Impl::length, Impl::seekable, nullptr, nullptr};
    char message[128]{};
    s.context = WavpackOpenFileInputEx64(&callbacks, &s.main, s.correction ? &s.extra : nullptr,
        message, OPEN_WVC, 0);
    check(s.error); require(s.context);
    require(WavpackGetNumErrors(s.context) == 0 && WavpackGetSampleIndex64(s.context) == 0,
        HRESULT_FROM_WIN32(ERROR_CRC));
    const auto mode = WavpackGetMode(s.context);
    require(!(WavpackGetQualifyMode(s.context) & QMODE_DSD_AUDIO), HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    const auto channels = WavpackGetNumChannels(s.context), bytes = WavpackGetBytesPerSample(s.context);
    const auto bits = WavpackGetBitsPerSample(s.context); auto samples = WavpackGetNumSamples64(s.context);
    const auto rate = WavpackGetSampleRate(s.context);
    require(channels >= 1 && channels <= 256 && bytes >= 1 && bytes <= 4 && bits > 0 && bits <= bytes * 8 && samples >= 0 && rate);
    require(uint64_t(rate) * channels * bytes <= MAXDWORD);
    format = {WORD(mode & MODE_FLOAT ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM), WORD(channels), rate,
        rate * DWORD(channels * bytes), WORD(channels * bytes), WORD(bytes * 8), 0};
    require(!(mode & MODE_FLOAT) || (bits == 32 && bytes == 4));
    valid_bits = WORD(bits); channel_mask = DWORD(WavpackGetChannelMask(s.context)); total = uint64_t(samples);
    const auto average = WavpackGetAverageBitrate(s.context, 1);
    require(std::isfinite(average)); bitrate = DWORD(std::min(std::max(0., average), double(MAXDWORD)));
    s.unpacked.resize(size_t(4096) * channels);
}
DWORD Decoder::read(BYTE* out, DWORD frames) {
    auto& s = *impl_; require(frames <= 4096);
    const auto errors = WavpackGetNumErrors(s.context);
    auto got = WavpackUnpackSamples(s.context, s.unpacked.data(), frames);
    check(s.error); require(WavpackGetNumErrors(s.context) == errors, HRESULT_FROM_WIN32(ERROR_CRC));
    const unsigned bytes = format.wBitsPerSample / 8;
    for (size_t i = 0; i < size_t(got) * format.nChannels; ++i) {
        // unpack.c already restores SHIFT_MASK to the original byte container.
        // Shifting 20-bit data again would destroy its most significant bits.
        auto value = uint32_t(s.unpacked[i]); if (bytes == 1) value += 128;
        for (unsigned b = 0; b < bytes; ++b) *out++ = BYTE(value >> (b * 8));
    }
    return got;
}
void Decoder::seek(uint64_t frame) {
    auto& s = *impl_; s.error = S_OK;
    const auto ok = WavpackSeekSample64(s.context, int64_t(frame)); check(s.error); require(ok != 0, STG_E_SEEKERROR);
}
}
