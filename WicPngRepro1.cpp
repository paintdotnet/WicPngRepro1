// How to use:
// 1. command-line argument argv[1] is path to the PNG image
// 2. CStreamWrapper::Seek() has a // TODO where you can set a breakpoint to 1) observe the overflow, and 2) test the hack that works around it
// 3. There's also a bug in that CopyPixels() will return S_OK even when the stream returns a failure HRESULT from Seek (see the very bottom of main())

#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00

#include <Windows.h>

#include <wincodec.h>

// ATL/COM
#include <comcat.h>
#define _ATL_FREE_THREADED
#include <atlbase.h>
#include <atlcoll.h>
#include <atlcom.h>
#include <atlcomcli.h>
#include <atlenc.h>

CComModule _Module;

// WIC
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

#include <iostream>
using namespace std;

#define IFS(hr, expr) \
    if (SUCCEEDED((hr))) \
    { \
        (hr) = (expr); \
    }

// Creates an instance of T and returns it to the caller with
// a ref count of 1. It will still need to be initialized.
template<typename T>
static HRESULT CreateComObject(T** ppResult)
{
    if (ppResult == NULL)
    {
        return E_POINTER;
    }

    *ppResult = NULL;

    HRESULT hr = S_OK;

    CComObject<T>* pObject = NULL;
    if (SUCCEEDED(hr))
    {
        hr = CComObject<T>::CreateInstance(&pObject);
    }

    if (SUCCEEDED(hr))
    {
        pObject->AddRef();
        *ppResult = pObject;
    }

    return hr;
}

// IStream wrapper
class ATL_NO_VTABLE CStreamWrapper
    : public CComObjectRootEx<CComMultiThreadModel>,
      public IStream
{
private:
    CComPtr<IStream> m_spStream;

public:
    DECLARE_NO_REGISTRY()

    BEGIN_COM_MAP(CStreamWrapper)
        COM_INTERFACE_ENTRY(IStream)
    END_COM_MAP()

    CStreamWrapper()
    {
    }

    virtual ~CStreamWrapper()
    {
    }

    HRESULT Initialize(IStream* pStream)
    {
        if (m_spStream)
        {
            return E_FAIL;
        }

        m_spStream = pStream;
        return S_OK;
    }

    // ISequentialStream
    HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcbRead)
    {
        //cout << "    Read()ing " << cb << " bytes" << endl;
        return m_spStream->Read(pv, cb, pcbRead);
    }

    HRESULT STDMETHODCALLTYPE Write(const void* pv, ULONG cb, ULONG* pcbWritten)
    {
        return m_spStream->Write(pv, cb, pcbWritten);
    }

    // IStream
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER* plibNewPosition)
    {
        //cout << "    Seeking to " << dlibMove.QuadPart << endl;

        if (dlibMove.QuadPart < 0)
        {
            // TODO: Set breakpoint here so you can observe the negative seek offset resulting from an integer overflow
            int z = 5; ++z;

            // Uncomment this line and things will actually decode properly...
            //dlibMove.HighPart = 0;
        }

        return m_spStream->Seek(dlibMove, dwOrigin, plibNewPosition);
    }

    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER libNewSize)
    {
        return m_spStream->SetSize(libNewSize);
    }

    HRESULT STDMETHODCALLTYPE CopyTo(IStream* pstm, ULARGE_INTEGER cb, ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWritten)
    {
        return m_spStream->CopyTo(pstm, cb, pcbRead, pcbWritten);
    }

    HRESULT STDMETHODCALLTYPE Commit(DWORD grfCommitFlags)
    {
        return m_spStream->Commit(grfCommitFlags);
    }

    HRESULT STDMETHODCALLTYPE Revert(void)
    {
        return m_spStream->Revert();
    }

    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
    {
        return m_spStream->LockRegion(libOffset, cb, dwLockType);
    }

    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType)
    {
        return m_spStream->UnlockRegion(libOffset, cb, dwLockType);
    }

    HRESULT STDMETHODCALLTYPE Stat(STATSTG* pstatstg, DWORD grfStatFlag)
    {
        return m_spStream->Stat(pstatstg, grfStatFlag);
    }

    HRESULT STDMETHODCALLTYPE Clone(IStream** ppstm)
    {
        return m_spStream->Clone(ppstm);
    }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        cout << "arg1 needs to be path to filename, and it needs to be a PNG";
        return 1;
    }

    cout << "File: " << argv[1] << endl;

    HRESULT hr = S_OK;

    IFS(hr, CoInitializeEx(NULL, COINIT_MULTITHREADED));

    CComPtr<IWICImagingFactory2> spFactory;
    IFS(hr, CoCreateInstance(
        CLSID_WICImagingFactory2,
        NULL,
        CLSCTX_INPROC_SERVER,
        __uuidof(IWICImagingFactory),
        reinterpret_cast<void**>(&spFactory)));

    CComPtr<IWICStream> spWicStream;
    IFS(hr, spFactory->CreateStream(&spWicStream));

    IFS(hr, spWicStream->InitializeFromFilename(
        argv[1], 
        GENERIC_READ));

    CComPtr<CStreamWrapper> spStreamWrapper;
    IFS(hr, CreateComObject<CStreamWrapper>(&spStreamWrapper));

    IFS(hr, spStreamWrapper->Initialize(spWicStream));

    CComPtr<IWICBitmapDecoder> spDecoder;
    IFS(hr, spFactory->CreateDecoder(
        GUID_ContainerFormatPng, 
        &GUID_VendorMicrosoftBuiltIn, 
        &spDecoder));

    IFS(hr, spDecoder->Initialize(
        spStreamWrapper,
        WICDecodeMetadataCacheOnDemand));

    CComPtr<IWICBitmapFrameDecode> spFrame0;
    IFS(hr, spDecoder->GetFrame(0, &spFrame0));

    UINT uiWidth = 0;
    UINT uiHeight = 0;
    IFS(hr, spFrame0->GetSize(&uiWidth, &uiHeight));
    if (SUCCEEDED(hr)) 
    {
        cout << "Image is " << uiWidth << " x " << uiHeight << endl;
    }

    WICPixelFormatGUID pixelFormat;
    IFS(hr, spFrame0->GetPixelFormat(&pixelFormat));

    UINT cbRow = uiWidth * 4;
    UINT uiBufferRows = 128;
    UINT cbBufferSize = cbRow * uiBufferRows;

    BYTE* pBuffer = NULL;
    if (SUCCEEDED(hr))
    {
        pBuffer = (BYTE*)malloc(cbBufferSize);
        if (pBuffer == NULL)
        {
            hr = E_OUTOFMEMORY;
        }
    }

    for (UINT y = 0; SUCCEEDED(hr) && y < uiHeight; y += uiBufferRows)
    {
        WICRect rc;
        rc.X = 0;
        rc.Y = y;
        rc.Width = uiWidth;

        UINT bottom0 = y + uiBufferRows;
        UINT bottom = min(bottom0, uiHeight);
        rc.Height = bottom - y;

        cout << "Copying " << rc.Height << " rows starting from y = " << y << endl;
        IFS(hr, spFrame0->CopyPixels(&rc, cbRow, cbBufferSize, pBuffer));

        // NOTE: At this point, if you saw the negative Seek and failure HRESULT from IStream::Seek(),
        //       you will also see that the HRESULT from CopyPixels is S_OK .....

        if (FAILED(hr))
        {
            cout << "    hr = " << hr;
        }
    }

    cout << "Done. hr = " << hr;

    return 0;
}
