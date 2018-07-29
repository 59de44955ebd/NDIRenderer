#include "renderer.h"
#include <initguid.h>
#include <Processing.NDI.Lib.h>

//######################################
// Defines
//######################################

// Define ASYNC_MODE to activate asynchronous mode. In async mode sample data is sent asynchronously,
// which unfortunately requires to copy it in memory, but performance is still better.

#define ASYNC_MODE

//######################################
// Globals
//######################################
NDIlib_send_instance_t g_pNDI_send = NULL;
NDIlib_video_frame_v2_t g_NDI_video_frame;

#ifdef ASYNC_MODE
PBYTE g_data = NULL;
#endif

//######################################
// GUIDs
//######################################

// {9EA28018-EE3C-4BC2-8FC1-9D89EB0F8C49}
DEFINE_GUID(CLSID_NDIRenderer,
	0x9ea28018, 0xee3c, 0x4bc2, 0x8f, 0xc1, 0x9d, 0x89, 0xeb, 0xf, 0x8c, 0x49);

//######################################
// Setup data
//######################################

const AMOVIESETUP_MEDIATYPE sudPinTypes = {
	&MEDIATYPE_Video,            // Major type
	&MEDIASUBTYPE_NULL          // Minor type
};

const AMOVIESETUP_PIN sudPins = {
	L"Input",                   // Name of the pin
	FALSE,                      // Is pin rendered
	FALSE,                      // Is an output pin
	FALSE,                      // Ok for no pins
	FALSE,                      // Allowed many
	&CLSID_NULL,                // Connects to filter
	L"Output",                  // Connects to pin
	1,                          // Number of pin types
	&sudPinTypes                // Details for pins
};

const AMOVIESETUP_FILTER sudSpoutRenderer = {
	&CLSID_NDIRenderer,      // Filter CLSID
	L"NDIRenderer",          // Filter name
	MERIT_DO_NOT_USE,          // Filter merit
	1,                         // Number pins
	&sudPins                   // Pin details
};

//######################################
// Notify about errors
//######################################
void ErrorMessage (const char * msg) {
	// print to debug log
	OutputDebugStringA(msg);

	// optional: show blocking message box?
	MessageBoxA(NULL, msg, "Error", MB_OK);
}

//######################################
// List of class IDs and creator functions for the class factory. This
// provides the link between the OLE entry point in the DLL and an object
// being created. The class factory will call the static CreateInstance
//######################################
CFactoryTemplate g_Templates[] = {
	{ L"NDIRenderer"
	, &CLSID_NDIRenderer
	, CVideoRenderer::CreateInstance
	, NULL
	, &sudSpoutRenderer }
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

//######################################
// CreateInstance
// This goes in the factory template table to create new filter instances
//######################################
CUnknown * WINAPI CVideoRenderer::CreateInstance (LPUNKNOWN pUnk, HRESULT *phr) {
	return new CVideoRenderer(NAME("NDIRenderer"), pUnk, phr);
}

//######################################
// Constructor
//######################################
CVideoRenderer::CVideoRenderer (TCHAR *pName, LPUNKNOWN pUnk, HRESULT *phr) :
	CBaseVideoRenderer(CLSID_NDIRenderer, pName, pUnk, phr),
	m_InputPin(NAME("Video Pin"), this, &m_InterfaceLock, phr, L"Input")
{
	// Store the video input pin
	m_pInputPin = &m_InputPin;

	// Not required, but "correct" (see the SDK documentation.
	if (!NDIlib_initialize()){
		ErrorMessage("Initializing NDILib failed");
	}

	// We create the NDI sender
	NDIlib_send_create_t params;
	params.p_ndi_name = "NDIRenderer";
	params.p_groups = NULL;
	params.clock_video = TRUE;
	params.clock_audio = FALSE;
	g_pNDI_send = NDIlib_send_create(&params);

	if (!g_pNDI_send) {
		ErrorMessage("Creating NDI sender failed");
	}
}

//######################################
// Destructor
//######################################
CVideoRenderer::~CVideoRenderer () {

	if (g_pNDI_send) {

		// Destroy the NDI sender
		NDIlib_send_destroy(g_pNDI_send);
		g_pNDI_send = NULL;

		// Not required, but nice
		NDIlib_destroy();
	}

#ifdef ASYNC_MODE
	if (g_data) {
		free(g_data);
		g_data = NULL;
	}
#endif

	m_pInputPin = NULL;
}

 //######################################
// CheckMediaType
// Check the proposed video media type
//######################################
HRESULT CVideoRenderer::CheckMediaType (const CMediaType *pMediaType) {

	// Does this have a VIDEOINFOHEADER format block
	const GUID *pFormatType = pMediaType->FormatType();
	if (*pFormatType != FORMAT_VideoInfo) {
		NOTE("Format GUID not a VIDEOINFOHEADER");
		return E_INVALIDARG;
	}
	ASSERT(pMediaType->Format());

	// Check the format looks reasonably ok
	ULONG Length = pMediaType->FormatLength();
	if (Length < SIZE_VIDEOHEADER) {
		NOTE("Format smaller than a VIDEOHEADER");
		return E_FAIL;
	}

	// Check if the media major type is MEDIATYPE_Video
	const GUID *pMajorType = pMediaType->Type();
	if (*pMajorType != MEDIATYPE_Video) {
		NOTE("Major type not MEDIATYPE_Video");
		return E_INVALIDARG;
	}

	// Check if the media subtype is supported
	const GUID *pSubType = pMediaType->Subtype();
	if (
		   *pSubType == MEDIASUBTYPE_UYVY      // NDIlib_FourCC_type_UYVY
		|| *pSubType == MEDIASUBTYPE_NV12      // NDIlib_FourCC_type_NV12
		|| *pSubType == MEDIASUBTYPE_RGB32     // NDIlib_FourCC_type_BGRX
		|| *pSubType == MEDIASUBTYPE_ARGB32    // NDIlib_FourCC_type_BGRA
		//|| * pSubType != MEDIASUBTYPE_YV12
	) return NOERROR;

	NOTE("Invalid video media subtype");
	return E_INVALIDARG;
}

//######################################
// GetPin
// We only support one input pin and it is numbered zero
//######################################
CBasePin *CVideoRenderer::GetPin (int n) {
	ASSERT(n == 0);
	if (n != 0) return NULL;

	// Assign the input pin if not already done so
	if (m_pInputPin == NULL) {
		m_pInputPin = &m_InputPin;
	}

	return m_pInputPin;
}

//######################################
// DoRenderSample
// Render the current image
//######################################
HRESULT CVideoRenderer::DoRenderSample (IMediaSample *pMediaSample) {

	CheckPointer(pMediaSample, E_POINTER);

	if (g_pNDI_send) {

		CAutoLock cInterfaceLock(&m_InterfaceLock);

		PBYTE pbData;
		HRESULT hr = pMediaSample->GetPointer(&pbData);
		if (FAILED(hr)) return hr;

		//send the frame via NDI
#ifdef ASYNC_MODE
		memcpy(g_data, pbData, pMediaSample->GetActualDataLength());
		g_NDI_video_frame.p_data = g_data;
		NDIlib_send_send_video_async_v2(g_pNDI_send, &g_NDI_video_frame);
#else
		g_NDI_video_frame.p_data = pbData;
		NDIlib_send_send_video_v2(g_pNDI_send, &g_NDI_video_frame);
#endif

	}

	return S_OK;
}

//######################################
// SetMediaType
// We store a copy of the media type used for the connection in the renderer
// because it is required by many different parts of the running renderer
// This can be called when we come to draw a media sample that has a format
// change with it. We normally delay type changes until they are really due
// for rendering otherwise we will change types too early if the source has
// allocated a queue of samples. In our case this isn't a problem because we
// only ever receive one sample at a time so it's safe to change immediately
//######################################
HRESULT CVideoRenderer::SetMediaType (const CMediaType *pMediaType) {
	CheckPointer(pMediaType, E_POINTER);
	HRESULT hr = NOERROR;
	CAutoLock cInterfaceLock(&m_InterfaceLock);
	CMediaType StoreFormat(m_mtIn);
	m_mtIn = *pMediaType;

	const GUID *pSubType = pMediaType->Subtype();
	if      (*pSubType == MEDIASUBTYPE_UYVY)   g_NDI_video_frame.FourCC = NDIlib_FourCC_type_UYVY;
	else if (*pSubType == MEDIASUBTYPE_NV12)   g_NDI_video_frame.FourCC = NDIlib_FourCC_type_NV12;
	else if (*pSubType == MEDIASUBTYPE_RGB32)  g_NDI_video_frame.FourCC = NDIlib_FourCC_type_BGRX; // vertically flipped
	else if (*pSubType == MEDIASUBTYPE_ARGB32) g_NDI_video_frame.FourCC = NDIlib_FourCC_type_BGRA; // vertically flipped
	//else if (*pSubType == MEDIASUBTYPE_YV12)  g_NDI_video_frame.FourCC = NDIlib_FourCC_type_YV12; // not working

	else {
		NOTE("Invalid video media subtype");
		return E_INVALIDARG;
	}

	return NOERROR;
}

//######################################
// BreakConnect
// This is called when a connection or an attempted connection is terminated
// and lets us to reset the connection flag held by the base class renderer
// The filter object may be hanging onto an image to use for refreshing the
// video window so that must be freed (the allocator decommit may be waiting
// for that image to return before completing) then we must also uninstall
// any palette we were using, reset anything set with the control interfaces
// then set our overall state back to disconnected ready for the next time
//######################################
HRESULT CVideoRenderer::BreakConnect () {
	CAutoLock cInterfaceLock(&m_InterfaceLock);

	// Check we are in a valid state
	HRESULT hr = CBaseVideoRenderer::BreakConnect();
	if (FAILED(hr)) return hr;

	// The window is not used when disconnected
	IPin *pPin = m_InputPin.GetConnected();
	if (pPin) SendNotifyWindow(pPin, NULL);

	return NOERROR;
}

//######################################
// CompleteConnect
// When we complete connection we need to see if the video has changed sizes
// If it has then we activate the window and reset the source and destination
// rectangles. If the video is the same size then we bomb out early. By doing
// this we make sure that temporary disconnections such as when we go into a
// fullscreen mode do not cause unnecessary property changes. The basic ethos
// is that all properties should be persistent across connections if possible
//######################################
HRESULT CVideoRenderer::CompleteConnect (IPin *pReceivePin) {

	CAutoLock cInterfaceLock(&m_InterfaceLock);

	CBaseVideoRenderer::CompleteConnect(pReceivePin);

	// Has the video size changed between connections?
	if ((m_mtIn.formattype == FORMAT_VideoInfo) && (m_mtIn.cbFormat == sizeof(VIDEOINFOHEADER) && (m_mtIn.pbFormat != NULL))) {
		VIDEOINFOHEADER *pVideoInfo = (VIDEOINFOHEADER *)m_mtIn.Format();

		g_NDI_video_frame.xres = pVideoInfo->bmiHeader.biWidth;
		g_NDI_video_frame.yres = pVideoInfo->bmiHeader.biHeight;
		if (g_NDI_video_frame.yres < 0) g_NDI_video_frame.yres = -g_NDI_video_frame.yres; // do we need this?

#ifdef ASYNC_MODE
		if (g_data) {
			g_data = (PBYTE)realloc(g_data, g_NDI_video_frame.xres * g_NDI_video_frame.yres * pVideoInfo->bmiHeader.biBitCount / 8);
		}
		else {
			g_data = (PBYTE)malloc(g_NDI_video_frame.xres * g_NDI_video_frame.yres * pVideoInfo->bmiHeader.biBitCount / 8);
		}
		if (!g_data) return E_OUTOFMEMORY;
#endif

		return NOERROR;
	}

	return E_INVALIDARG;
}

//######################################
// Constructor
//######################################
CVideoInputPin::CVideoInputPin (TCHAR *pObjectName,
		CVideoRenderer *pRenderer,
		CCritSec *pInterfaceLock,
		HRESULT *phr,
		LPCWSTR pPinName) :
	CRendererInputPin(pRenderer, phr, pPinName),
	m_pRenderer(pRenderer),
	m_pInterfaceLock(pInterfaceLock)
{
	ASSERT(m_pRenderer);
	ASSERT(pInterfaceLock);
}

////////////////////////////////////////////////////////////////////////
// Exported entry points for registration and unregistration
// (in this case they only call through to default implementations).
////////////////////////////////////////////////////////////////////////

//######################################
// DllRegisterSever
//######################################
STDAPI DllRegisterServer () {
	return AMovieDllRegisterServer2(TRUE);
}

//######################################
// DllUnregisterServer
//######################################
STDAPI DllUnregisterServer () {
	return AMovieDllRegisterServer2(FALSE);
}

//######################################
// DllEntryPoint
//######################################
extern "C" BOOL WINAPI DllEntryPoint (HINSTANCE, ULONG, LPVOID);
BOOL APIENTRY DllMain(HANDLE hModule, DWORD  dwReason, LPVOID lpReserved) {
	return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}
