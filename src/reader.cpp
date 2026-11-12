#include "codec.h"
#include "metadata.h"
#include "ttpcomm/audio_format_probe_impl.h"
#include "ttpcomm/pcm_format.h"
namespace ttp::TTP_NAMESPACE {
class AudioReader final : public Reader, public Metadata, public Thumbnail,
                          public MetadataCommit, public ttp::format::InfoSupport {
    LONG refs_{1};
    std::unique_ptr<Input> input_;
    std::unique_ptr<Decoder> decoder_;
    WAVEFORMATEX format_{};
    WORD valid_bits_{};
    DWORD mask_{}, bitrate_{};
    uint64_t total_{}, position_{};
    bool opened_{}, committed_{}, retry_{};
    HRESULT commit_result_{S_OK}, error_{S_OK};
    Tags tags_;
    Picture picture_{};
    std::wstring picture_mime_, picture_description_;
    void reopen(uint64_t frame) {
        decoder_.reset();
        auto next = std::make_unique<Decoder>();
        next->open(*input_);
        require(!memcmp(&next->format, &format_, sizeof(format_)) && next->total == total_ &&
                next->valid_bits == valid_bits_ && next->channel_mask == mask_, STG_E_REVERTED);
        if (frame < total_) next->seek(frame);
        decoder_ = std::move(next); position_ = frame; retry_ = false; error_ = S_OK;
    }
    template<class T> HRESULT property(T* p, T value) {
        if (!p) return E_POINTER;
        *p = {};
        if (!opened_ || committed_) return E_UNEXPECTED;
        *p = value; return S_OK;
    }
    std::vector<std::pair<std::wstring, std::wstring>> text_tags() {
        std::vector<std::pair<std::wstring, std::wstring>> result;
        for (auto& t : tags_.items) if (!(t.flags & 6))
            for (auto& v : tags_.values(t)) result.emplace_back(wide(t.key), std::move(v));
        return result;
    }
    static const char* cover_name(DWORD type) {
        static const char* names[]{"Other","File Icon","Other File Icon","Front","Back","Leaflet","Media",
            "Lead Artist","Artist","Conductor","Band","Composer","Lyricist","Recording Location",
            "During Recording","During Performance","Movie Capture","Coloured Fish","Illustration","Band Logo","Publisher Logo"};
        return type < 21 ? names[type] : nullptr;
    }
    static DWORD cover_type(const Tag& t) {
        for (DWORD i = 0; i < 21; ++i)
            if (Tags::equal(t.key, (std::string("Cover Art (") + cover_name(i) + ")").c_str())) return i;
        return 0;
    }
    std::vector<Tag*> pictures() {
        std::vector<Tag*> result;
        for (auto& t : tags_.items) if ((t.flags & 6) == 2 && !_strnicmp(t.key.c_str(), "Cover Art (", 11)) {
            auto end = std::find(t.value.begin(), t.value.end(), 0);
            if (end != t.value.end() && end + 1 != t.value.end()) result.push_back(&t);
        }
        return result;
    }
    void write_picture(DWORD index, const Picture* p, bool replace) {
        require(opened_ && !committed_, E_UNEXPECTED);
        require(input_->capabilities(true) & 16, STG_E_ACCESSDENIED);
        require(p && p->size >= sizeof(Picture) && p->data && p->bytes &&
                p->bytes <= 16 * 1024 * 1024 - 1024 && cover_name(p->type), E_INVALIDARG);
        auto all = pictures(); require(replace ? index < all.size() : all.size() < 21, E_INVALIDARG);
        auto key = std::string("Cover Art (") + cover_name(p->type) + ")";
        auto existing = tags_.find(key.c_str()); require(!existing || (replace && existing == all[index]), E_INVALIDARG);
        auto filename = utf8(p->description && *p->description ? p->description : L"cover");
        Bytes value(filename.begin(), filename.end()); value.push_back(0); value.insert(value.end(), p->data, p->data + p->bytes);
        Tag tag{key, std::move(value), 2};
        if (replace) *all[index] = std::move(tag); else tags_.items.push_back(std::move(tag));
        tags_.dirty = true;
    }
public:
    ~AudioReader() { if (opened_ && !committed_) Commit(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** p) override {
        if (!p) return E_POINTER;
        *p = nullptr;
        if (same(iid, IID_IUnknown) || same(iid, iid_reader)) *p = static_cast<Reader*>(this);
        else if (same(iid, iid_metadata)) *p = static_cast<Metadata*>(this);
        else if (same(iid, iid_thumbnail)) *p = static_cast<Thumbnail*>(this);
        else if (same(iid, iid_metadata_commit)) *p = static_cast<MetadataCommit*>(this);
        else if (same(iid, ttp::format::iid_info)) *p = static_cast<ttp::format::SourceInfo*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override { auto n = InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Open(IStream* s, DWORD) override {
        if (opened_ || input_ || committed_) return E_UNEXPECTED;
        const auto probe = PrepareFormatInfo(s, codec_family); if (FAILED(probe)) return probe;
        auto hr = protect([&] {
            input_ = std::make_unique<Input>(); input_->open(s);
            require(input_->seekable, STG_E_INVALIDFUNCTION);
            require(input_->size <= INT64_MAX && input_->size >= 16, HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            tags_.load(*input_);
            decoder_ = std::make_unique<Decoder>(); decoder_->open(*input_);
            format_ = decoder_->format; valid_bits_ = decoder_->valid_bits;
            mask_ = decoder_->channel_mask; total_ = decoder_->total; bitrate_ = decoder_->bitrate;
            opened_ = true; return S_OK;
        });
        if (FAILED(hr)) { decoder_.reset(); input_.reset(); tags_ = Tags{}; }
        return CompleteFormatInfo(hr);
    }
    HRESULT STDMETHODCALLTYPE Capabilities(DWORD* p) override { return property(p, input_ ? input_->capabilities(true) : 0u); }
    HRESULT STDMETHODCALLTYPE Duration(DWORD* p) override { return property(p, format_.nSamplesPerSec ? milliseconds(total_, format_.nSamplesPerSec) : 0u); }
    HRESULT STDMETHODCALLTYPE Format(WAVEFORMATEX** p) override {
        if (!p) return E_POINTER; *p = nullptr;
        if (!opened_ || committed_) return E_UNEXPECTED;
        return ttp::pcm::copy_format(format_, mask_, valid_bits_, p);
    }
    HRESULT STDMETHODCALLTYPE BufferSize(DWORD* p) override { return property(p, DWORD(4096 * format_.nBlockAlign)); }
    HRESULT STDMETHODCALLTYPE CodecName(wchar_t** p) override { return text(codec_title, p); }
    HRESULT STDMETHODCALLTYPE Bitrate(DWORD* p) override { return property(p, bitrate_); }
    HRESULT STDMETHODCALLTYPE BitDepth(WORD* p) override { return property(p, valid_bits_); }
    HRESULT STDMETHODCALLTYPE SetCallback(IUnknown* p) override { return input_ ? input_->callback(p) : E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE Start() override { if (!opened_ || committed_) return E_UNEXPECTED; ResetEvent(input_->event); return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop() override { if (!opened_ || committed_) return E_UNEXPECTED; SetEvent(input_->event); return S_OK; }
    HRESULT STDMETHODCALLTYPE Read(Buffer* b) override {
        return protect([&]() -> HRESULT {
            require(b, E_POINTER); check(b->SetLength(0)); require(opened_ && !committed_, E_UNEXPECTED);
            require(WaitForSingleObject(input_->event, 0) != WAIT_OBJECT_0, E_ABORT);
            DWORD capacity{}, length{}; BYTE* out{};
            check(b->Capacity(&capacity)); check(b->Data(&out, &length));
            require(out && capacity >= format_.nBlockAlign, E_INVALIDARG);
            if (retry_) reopen(position_);
            check(error_);
            if (position_ == total_) return S_FALSE;
            const auto wanted = DWORD(std::min<uint64_t>(std::min<DWORD>(capacity / format_.nBlockAlign, 4096), total_ - position_));
            DWORD got{};
            auto hr = protect([&] { got = decoder_->read(out, wanted); require(got && got <= wanted, STG_E_READFAULT); return S_OK; });
            if (FAILED(hr)) {
                retry_ = hr == E_ABORT || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED);
                if (!retry_) error_ = hr;
                return hr;
            }
            check(b->SetLength(got * format_.nBlockAlign)); position_ += got; return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Seek(DWORD* ms) override {
        return protect([&] {
            require(ms, E_POINTER); require(opened_ && !committed_, E_UNEXPECTED);
            const auto target = std::min<uint64_t>(uint64_t(*ms) * format_.nSamplesPerSec / 1000, total_);
            ResetEvent(input_->event); retry_ = true;
            reopen(target); *ms = milliseconds(target, format_.nSamplesPerSec); return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Count(DWORD* p) override { return protect([&] { require(p, E_POINTER); *p = DWORD(text_tags().size()); return S_OK; }); }
    HRESULT STDMETHODCALLTYPE At(DWORD i, wchar_t** k, wchar_t** v) override {
        if (!k || !v) return E_POINTER; *k = *v = nullptr;
        return protect([&] { auto tags = text_tags(); require(i < tags.size(), E_INVALIDARG); check(text(tags[i].first, k));
            auto hr = text(tags[i].second, v); if (FAILED(hr)) { CoTaskMemFree(*k); *k = nullptr; } return hr; });
    }
    HRESULT STDMETHODCALLTYPE Get(const char* k, wchar_t** v) override {
        if (!v) return E_POINTER; *v = nullptr;
        return protect([&] { require(k, E_POINTER); auto t = tags_.find(k); require(t && !(t->flags & 6), E_INVALIDARG); return text(tags_.value(*t), v); });
    }
    HRESULT STDMETHODCALLTYPE Set(const char* k, const wchar_t* v) override {
        return protect([&] { require(opened_ && !committed_, E_UNEXPECTED); require(input_->capabilities(false) & 4, STG_E_ACCESSDENIED);
            if (!k && !v) tags_.clear(); else tags_.set(k, v); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE Commit() override {
        if (committed_) return commit_result_;
        if (!opened_) return E_UNEXPECTED;
        committed_ = true; decoder_.reset(); commit_result_ = tags_.save(*input_); return commit_result_;
    }
    HRESULT STDMETHODCALLTYPE PictureCount(DWORD* p) override { return protect([&] { require(p, E_POINTER); *p = DWORD(pictures().size()); return S_OK; }); }
    HRESULT STDMETHODCALLTYPE MaximumBytes(DWORD* p) override { if (!p) return E_POINTER; *p = 16 * 1024 * 1024 - 1024; return S_OK; }
    HRESULT STDMETHODCALLTYPE MaximumCount(DWORD* p) override { if (!p) return E_POINTER; *p = 21; return S_OK; }
    HRESULT STDMETHODCALLTYPE PictureAt(DWORD i, const Picture** p) override {
        if (!p) return E_POINTER; *p = nullptr;
        return protect([&] {
            auto all = pictures(); require(i < all.size(), E_INVALIDARG); auto& t = *all[i];
            auto end = std::find(t.value.begin(), t.value.end(), 0); auto offset = size_t(end - t.value.begin()) + 1;
            picture_description_ = wide(std::string(t.value.begin(), end)); auto data = t.value.data() + offset; auto n = t.value.size() - offset;
            picture_mime_ = n >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8) ? L"image/png" : n >= 2 && data[0] == 255 && data[1] == 216 ? L"image/jpeg" : L"application/octet-stream";
            picture_ = {sizeof(Picture), picture_mime_.c_str(), picture_description_.c_str(), DWORD(n), data, cover_type(t)};
            *p = &picture_; return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE AddPicture(const Picture* p) override { return protect([&] { write_picture(0, p, false); return S_OK; }); }
    HRESULT STDMETHODCALLTYPE ReplacePicture(DWORD i, const Picture* p, DWORD mask) override {
        return protect([&] { require(p && p->size >= sizeof(Picture) && !(mask & ~15u), E_INVALIDARG);
            const Picture* old{}; check(PictureAt(i, &old)); auto merged = *old;
            if (mask & 1) merged.mime = p->mime; if (mask & 2) merged.description = p->description;
            if (mask & 4) merged.type = p->type; if (mask & 8) { merged.data = p->data; merged.bytes = p->bytes; }
            write_picture(i, &merged, true); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE RemovePicture(DWORD i) override {
        return protect([&] { require(opened_ && !committed_, E_UNEXPECTED); require(input_->capabilities(true) & 16, STG_E_ACCESSDENIED);
            auto all = pictures(); require(i < all.size(), E_INVALIDARG);
            tags_.items.erase(tags_.items.begin() + (all[i] - tags_.items.data())); tags_.dirty = true; return S_OK; });
    }
};
HRESULT make_reader(void** p) {
    if (!p) return E_POINTER; *p = nullptr;
    return protect([&] { *p = static_cast<Reader*>(new AudioReader); return S_OK; });
}
}
