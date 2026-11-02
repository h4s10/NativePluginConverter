// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>
#include <commctrl.h>
#include <atomic>
#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

// GUI-thread-only registration for the bridge's own window subtree. The source
// keeps ownership of OLE data; WM_DROPFILES transfers ownership of its HDROP.
class FileDropTarget final : public IDropTarget {
public:
    using Sink = bool (*)(void*, const std::wstring&);
    // replaceExisting is reserved for controls whose native drop target requires
    // FL's channel-sample service. Other editors retain their working targets.
    FileDropTarget(void* context, Sink sink, bool replaceExisting = false, bool nativeAudioFormats = false, bool recursive = true)
        : context(context), sink(sink), replaceExisting(replaceExisting), nativeAudioFormats(nativeAudioFormats), recursive(recursive) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IDropTarget)) {
            *out = static_cast<IDropTarget*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        if (!effect) return E_POINTER;
        candidate.clear();
        try { if (sink) candidate = readData(data); } catch (...) {}
        *effect = !candidate.empty() && (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        if (!effect) return E_POINTER;
        *effect = sink && !candidate.empty() && (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override { candidate.clear(); return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        if (!effect) return E_POINTER;
        const bool copy = (*effect & DROPEFFECT_COPY) != 0;
        *effect = DROPEFFECT_NONE;
        try {
            // Request data again at Drop: some sources materialize the file late.
            auto path = readData(data);
            if (copy && sink && !path.empty() && sink(context, path)) *effect = DROPEFFECT_COPY;
        } catch (...) {}
        candidate.clear();
        return S_OK;
    }
    // Adapt standard OLE files only to a control that already implements the
    // native WM_DROPFILES protocol. Ownership transfers on successful posting.
    static bool postLegacyDrop(void* context,const std::wstring& path) {
        HWND target=static_cast<HWND>(context);if(!IsWindow(target))return false;
        const size_t bytes=sizeof(DROPFILES)+(path.size()+2)*sizeof(wchar_t);
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,bytes);if(!memory)return false;
        auto* data=static_cast<DROPFILES*>(GlobalLock(memory));if(!data){GlobalFree(memory);return false;}
        data->pFiles=sizeof(DROPFILES);data->fWide=TRUE;
        std::memcpy(reinterpret_cast<char*>(data)+sizeof(DROPFILES),path.c_str(),(path.size()+1)*sizeof(wchar_t));
        GlobalUnlock(memory);
        if(PostMessageW(target,WM_DROPFILES,reinterpret_cast<WPARAM>(memory),0))return true;
        GlobalFree(memory);return false;
    }
    void attach(HWND root) {
        oleReady = SUCCEEDED(OleInitialize(nullptr));
        refresh(root);
    }
    void refresh(HWND root) {
        if (!sink || !root) return;
        install(root);
        if (!recursive) return;
        EnumChildWindows(root, [](HWND hwnd, LPARAM param) -> BOOL {
            reinterpret_cast<FileDropTarget*>(param)->install(hwnd); return TRUE;
        }, reinterpret_cast<LPARAM>(this));
    }
    void detach() {
        sink = nullptr; context = nullptr; candidate.clear();
        while (!windows.empty()) {
            auto entry = windows.back(); windows.pop_back();
            if (IsWindow(entry.hwnd)) {
                RemoveWindowSubclass(entry.hwnd, windowProc, reinterpret_cast<UINT_PTR>(this));
                if (entry.ole) RevokeDragDrop(entry.hwnd);
                DragAcceptFiles(entry.hwnd, entry.acceptedBefore);
            }
        }
        if (oleReady) { OleUninitialize(); oleReady = false; }
    }
    size_t registeredWindows() const { return windows.size(); }
    size_t oleWindows() const { return std::count_if(windows.begin(), windows.end(), [](const Entry& e){ return e.ole; }); }
private:
    struct Entry { HWND hwnd; bool ole; BOOL acceptedBefore; };
    std::atomic<ULONG> refs{1};
    void* context;
    Sink sink;
    bool oleReady = false;
    const bool replaceExisting;
    const bool nativeAudioFormats;
    const bool recursive;
    std::wstring candidate;
    std::vector<Entry> windows;
    bool accepts(const std::wstring& path) const {
        auto dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos) return false;
        const auto* ext = path.c_str() + dot;
        if (_wcsicmp(ext, L".wav") == 0 || _wcsicmp(ext, L".wave") == 0) return true;
        if (!nativeAudioFormats) return false;
        for (const auto* type : {L".aif", L".aiff", L".flac", L".ogg", L".mp3", L".wv"})
            if (_wcsicmp(ext, type) == 0) return true;
        return false;
    }
    std::wstring readDrop(HDROP drop) const {
        if (!drop) return {};
        const UINT count = std::min(DragQueryFileW(drop, 0xffffffff, nullptr, 0), UINT(4096));
        for (UINT i = 0; i < count; ++i) {
            UINT n = DragQueryFileW(drop, i, nullptr, 0);
            if (!n || n >= 32768) continue;
            std::wstring path(n + 1, L'\0');
            if (DragQueryFileW(drop, i, path.data(), n + 1) != n) continue;
            path.resize(n);
            if (accepts(path)) return path;
        }
        return {};
    }
    std::wstring readData(IDataObject* data) const {
        if (!data) return {};
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (FAILED(data->GetData(&format, &medium))) return {};
        struct Release { STGMEDIUM& m; ~Release(){ ReleaseStgMedium(&m); } } release{medium};
        if (medium.tymed != TYMED_HGLOBAL || !medium.hGlobal) return {};
        return readDrop(static_cast<HDROP>(medium.hGlobal));
    }
    void install(HWND hwnd) {
        if (GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) return;
        if (std::any_of(windows.begin(), windows.end(), [hwnd](const Entry& e){return e.hwnd == hwnd;})) return;
        // Allocate tracking first, before taking ownership of any window hook.
        try { windows.push_back({hwnd, false, BOOL((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) != 0)}); }
        catch (...) { return; }
        if (!SetWindowSubclass(hwnd, windowProc, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this))) { windows.pop_back(); return; }
        DragAcceptFiles(hwnd, TRUE);
        if (oleReady) {
            HRESULT result = RegisterDragDrop(hwnd, this);
            // Never revoke a native sampler's working OLE target. Replacement
            // is opt-in for the original FL-specific host-sample protocol only.
            if (replaceExisting && result == DRAGDROP_E_ALREADYREGISTERED && SUCCEEDED(RevokeDragDrop(hwnd)))
                result = RegisterDragDrop(hwnd, this);
            windows.back().ole = SUCCEEDED(result);
        }
    }
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
        auto* self = reinterpret_cast<FileDropTarget*>(data);
        if (msg == WM_DROPFILES) {
            auto entry = std::find_if(self->windows.begin(), self->windows.end(), [hwnd](const Entry& e){return e.hwnd == hwnd;});
            if (!self->replaceExisting && entry != self->windows.end() && entry->acceptedBefore)
                return DefSubclassProc(hwnd, msg, wp, lp); // native handler owns HDROP
            std::wstring path;
            try { path = self->readDrop(reinterpret_cast<HDROP>(wp)); } catch (...) {}
            DragFinish(reinterpret_cast<HDROP>(wp));
            try { if (self->sink && !path.empty()) self->sink(self->context, path); } catch (...) {}
            return 0;
        }
        if (msg == WM_NCDESTROY) {
            auto it = std::find_if(self->windows.begin(), self->windows.end(), [hwnd](const Entry& e){return e.hwnd == hwnd;});
            if (it != self->windows.end()) {
                bool ole = it->ole; self->windows.erase(it);
                if (ole) RevokeDragDrop(hwnd);
            }
            RemoveWindowSubclass(hwnd, windowProc, id);
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
};
