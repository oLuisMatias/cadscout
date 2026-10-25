// Explorer right-click entry "View with CADscout" for the Microsoft Store package of CADscout Lite.
//
// Windows loads this DLL (declared in msix/AppxManifest.xml as a COM server) when the user right-clicks a model file. It
// shows one command with the CADscout icon; clicking it starts CADscout Lite with the file. It does not use Qt.
#include <windows.h>
#include <appmodel.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <string>

// Must match the Clsid values in msix/AppxManifest.xml.
static const CLSID CLSID_ViewWithCadscout = {
    0x615599F7, 0x62BB, 0x47E5, {0x8F, 0xB5, 0x81, 0xCF, 0x4C, 0x0B, 0x1D, 0xA8}};

static HMODULE g_module = nullptr;
static std::atomic<long> g_objects{0};

namespace {

// The model file types of the viewer (keep in sync with src/Formats.h). The entry is declared for all files in the package
// manifest and hidden by GetState for any other type.
bool isModelFile(IShellItemArray* items) {
    if (!items) return false;
    DWORD count = 0;
    if (FAILED(items->GetCount(&count)) || count == 0) return false;
    IShellItem* item = nullptr;
    if (FAILED(items->GetItemAt(0, &item))) return false;
    PWSTR file = nullptr;
    const HRESULT got = item->GetDisplayName(SIGDN_FILESYSPATH, &file);
    item->Release();
    if (FAILED(got)) return false;
    std::wstring extension = PathFindExtensionW(file);
    CoTaskMemFree(file);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
    static const wchar_t* const known[] = {L".stp", L".step", L".igs", L".iges", L".brep", L".stl", L".obj",
                                           L".ply", L".wrl",  L".vrml", L".gltf", L".glb",  L".3mf", L".dxf"};
    for (const wchar_t* k : known)
        if (extension == k) return true;
    return false;
}

// "<folder of this DLL>\CADscoutLite.exe,0": the program file holds the icon (resource 0).
std::wstring iconLocation() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(g_module, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring folder(path, n);
    folder.resize(folder.find_last_of(L'\\') + 1);
    return folder + L"CADscoutLite.exe,0";
}

// Id of the installed app ("<package family name>!CADscoutLite"); this DLL runs inside the package, so its identity is known.
std::wstring appUserModelId() {
    UINT32 length = 0;
    GetCurrentPackageFamilyName(&length, nullptr);
    if (length == 0) return L"";
    std::wstring family(length, L'\0');
    if (GetCurrentPackageFamilyName(&length, family.data()) != ERROR_SUCCESS) return L"";
    family.resize(wcslen(family.c_str()));
    return family + L"!CADscoutLite";
}

class ViewCommand final : public IExplorerCommand, public IObjectWithSite {
public:
    ViewCommand() { ++g_objects; }

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IExplorerCommand) *out = static_cast<IExplorerCommand*>(this);
        else if (riid == IID_IObjectWithSite) *out = static_cast<IObjectWithSite*>(this);
        else { *out = nullptr; return E_NOINTERFACE; }
        AddRef();
        return S_OK;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    IFACEMETHODIMP_(ULONG) Release() override {
        const ULONG r = --refs_;
        if (r == 0) {
            if (site_) site_->Release();
            --g_objects;
            delete this;
        }
        return r;
    }

    // IExplorerCommand
    IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* name) override { return SHStrDupW(L"View with CADscout", name); }
    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override {
        const std::wstring location = iconLocation();
        if (location.empty()) { *icon = nullptr; return E_FAIL; }
        return SHStrDupW(location.c_str(), icon);
    }
    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* tip) override { *tip = nullptr; return E_NOTIMPL; }
    IFACEMETHODIMP GetCanonicalName(GUID* guid) override { *guid = CLSID_ViewWithCadscout; return S_OK; }
    IFACEMETHODIMP GetState(IShellItemArray* items, BOOL, EXPCMDSTATE* state) override {
        *state = isModelFile(items) ? ECS_ENABLED : ECS_HIDDEN;
        return S_OK;
    }
    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override { *flags = ECF_DEFAULT; return S_OK; }
    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** e) override { *e = nullptr; return E_NOTIMPL; }

    IFACEMETHODIMP Invoke(IShellItemArray* items, IBindCtx*) override {
        if (!items) return E_INVALIDARG;
        DWORD count = 0;
        items->GetCount(&count);
        if (count == 0) return S_OK;
        IShellItem* item = nullptr;
        if (FAILED(items->GetItemAt(0, &item))) return E_FAIL;  // CADscout Lite shows one model: the first selected file
        PWSTR file = nullptr;
        const HRESULT got = item->GetDisplayName(SIGDN_FILESYSPATH, &file);
        item->Release();
        if (FAILED(got)) return got;
        const std::wstring arguments = L"\"" + std::wstring(file) + L"\"";
        CoTaskMemFree(file);

        const std::wstring aumid = appUserModelId();
        if (aumid.empty()) return E_FAIL;
        IApplicationActivationManager* manager = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&manager));
        if (FAILED(hr)) return hr;
        DWORD pid = 0;
        hr = manager->ActivateApplication(aumid.c_str(), arguments.c_str(), AO_NONE, &pid);
        manager->Release();
        return hr;
    }

    // IObjectWithSite
    IFACEMETHODIMP SetSite(IUnknown* site) override {
        if (site_) site_->Release();
        site_ = site;
        if (site_) site_->AddRef();
        return S_OK;
    }
    IFACEMETHODIMP GetSite(REFIID riid, void** out) override {
        if (!site_) { *out = nullptr; return E_FAIL; }
        return site_->QueryInterface(riid, out);
    }

private:
    ~ViewCommand() = default;
    std::atomic<ULONG> refs_{1};
    IUnknown* site_ = nullptr;
};

class Factory final : public IClassFactory {
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *out = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return 2; }   // lives as long as the DLL
    IFACEMETHODIMP_(ULONG) Release() override { return 1; }
    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* command = new (std::nothrow) ViewCommand;
        if (!command) return E_OUTOFMEMORY;
        const HRESULT hr = command->QueryInterface(riid, out);
        command->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL lock) override {
        if (lock) ++g_objects; else --g_objects;
        return S_OK;
    }
};

Factory g_factory;

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

extern "C" HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID clsid, REFIID riid, void** out) {
    if (clsid != CLSID_ViewWithCadscout) { *out = nullptr; return CLASS_E_CLASSNOTAVAILABLE; }
    return g_factory.QueryInterface(riid, out);
}

extern "C" HRESULT STDAPICALLTYPE DllCanUnloadNow() { return g_objects.load() == 0 ? S_OK : S_FALSE; }
