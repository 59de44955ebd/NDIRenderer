#pragma once

#include <streams.h>


// Forward declarations
class CVideoRenderer;
class CVideoInputPin;
class CControlVideo;

//######################################
// This class supports the renderer input pin. We have to override the base
// class input pin because we provide our own special allocator which hands
// out buffers based on GDI DIBSECTIONs. We have an extra limitation which
// is that we only connect to filters that agree to use our allocator. This
// stops us from connecting to the tee for example. The extra work required
// to use someone elses allocator and select the buffer into a bitmap and
// that into the HDC is not great but would only really confuse this sample
//######################################
class CVideoInputPin : public CRendererInputPin
{
	CVideoRenderer *m_pRenderer;        // The renderer that owns us
	CCritSec *m_pInterfaceLock;         // Main filter critical section

public:
	// Constructor
	CVideoInputPin(
		TCHAR *pObjectName,             // Object string description
		CVideoRenderer *pRenderer,      // Used to delegate locking
		CCritSec *pInterfaceLock,       // Main critical section
		HRESULT *phr,                   // OLE failure return code
		LPCWSTR pPinName);              // This pins identification
};

//######################################
// This is the COM object that represents a simple rendering filter. It
// supports IBaseFilter and IMediaFilter and a single input stream (pin)
// The classes that support these interfaces have nested scope NOTE the
// nested class objects are passed a pointer to their owning renderer
// when they are created but they should not use it during construction
//######################################
class CVideoRenderer : public CBaseVideoRenderer
{
public:

	// Constructor and destructor
	static CUnknown * WINAPI CreateInstance(LPUNKNOWN, HRESULT *);

	CVideoRenderer(TCHAR *pName, LPUNKNOWN pUnk, HRESULT *phr);
	~CVideoRenderer();

	CBasePin *GetPin(int n);

	// Override these from the filter and renderer classes
	HRESULT BreakConnect();
	HRESULT CompleteConnect(IPin *pReceivePin);
	HRESULT SetMediaType(const CMediaType *pMediaType);
	HRESULT DoRenderSample(IMediaSample *pMediaSample);
	HRESULT CheckMediaType(const CMediaType *pMediaType);

public:
	CVideoInputPin  m_InputPin;        // IPin based interfaces
	CMediaType      m_mtIn;            // Source connection media type
};
