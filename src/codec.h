#pragma once
#include "io.h"
#include "ttpcomm/audio_format_probe.h"
namespace ttp::TTP_NAMESPACE {
#if defined(TTP_WAVPACK)
inline constexpr DWORD codec_family = ttp::format::WavPack;
inline constexpr auto codec_title = L"WV|WavPack Audio";
inline constexpr auto reader_name = L"WavPack Reader";
inline constexpr auto codec_extensions = L"WavPack Audio(*.wv)";
#else
inline constexpr DWORD codec_family = ttp::format::OptimFrog;
inline constexpr auto codec_title = L"OFR|OptimFROG Audio";
inline constexpr auto reader_name = L"OptimFROG Reader";
inline constexpr auto codec_extensions = L"OptimFROG Audio(*.ofr;*.ofs)";
#endif
// Each repository supplies its own backend. No native decoder types cross ABI.
class Decoder {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    WAVEFORMATEX format{};
    WORD valid_bits{};
    DWORD channel_mask{}, bitrate{};
    uint64_t total{};
    Decoder();
    ~Decoder();
    void open(Input&);
    DWORD read(BYTE*, DWORD frames);
    void seek(uint64_t frame);
};
}
