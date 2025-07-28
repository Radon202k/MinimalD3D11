// Based on https://gist.github.com/mmozeiko/5e727f845db182d468a34d524508ad5f

// NOTE: This is a pretty rough sketch as of now... I'll be updating this
// once I get a better idea of how I want to structure my renderer.

#define COBJMACROS
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dxgidebug.h>
#include <d3dcompiler.h>
#include <stdbool.h>
#include <assert.h>

#define _USE_MATH_DEFINES
#include <math.h>

#pragma comment(lib, "user32")
#pragma comment(lib, "d3d11")
#pragma comment(lib, "dxguid")
#pragma comment(lib, "dxgi")
#pragma comment(lib, "d3dcompiler")

typedef struct
{
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGIDevice *dxgiDevice;
    IDXGIAdapter *dxgiAdapter;
    IDXGIFactory2 *dxgiFactory;
    
} D3D11Context;

typedef struct
{
    HWND handle;
    WNDCLASSEXA wndClass;
    UINT width;
    UINT height;
    
} D3D11Window;

typedef struct
{
    float position[2];
    float uv[2];
    float color[3];
    
} D3D11Vertex;

typedef struct
{
    ID3D11InputLayout *layout;
    ID3D11VertexShader *vertex;
    ID3D11PixelShader *pixel;
    
} D3D11Shader;

typedef struct
{
    ID3D11Texture2D *handle;
    ID3D11ShaderResourceView *view;
    
} D3D11Texture;

typedef struct
{
    IDXGISwapChain1 *swapChain;
    ID3D11SamplerState *sampler;
    ID3D11BlendState *blend;
    ID3D11RasterizerState *rasterizer;
    ID3D11DepthStencilState *ds;
    ID3D11RenderTargetView* rtView;
    ID3D11DepthStencilView* dsView;
    
} D3D11Pipeline;

typedef struct
{
    bool running;
    D3D11Context d3d11;
    ID3D11Buffer *vertexBuffer;
    ID3D11Buffer *indexBuffer;
    unsigned short indexCount;
    ID3D11Buffer *uniformBuffer;
    D3D11Shader shader;
    D3D11Texture texture;
    D3D11Pipeline pipeline;
    D3D11Window window;
    
} AppState;

void
print(char *msg)
{
    OutputDebugStringA(msg);
}

void
d3d11_destroy(D3D11Context *d3d11)
{
    assert(d3d11->context);
    ID3D11DeviceContext_Release(d3d11->context);
    d3d11->context = 0;
    
    assert(d3d11->device);
    ID3D11Device_Release(d3d11->device);
    d3d11->device = 0;
    
    assert(d3d11->dxgiDevice);
    IDXGIDevice_Release(d3d11->dxgiDevice);
    d3d11->dxgiDevice = 0;
    
    assert(d3d11->dxgiAdapter);
    IDXGIAdapter_Release(d3d11->dxgiAdapter);
    d3d11->dxgiAdapter = 0;
    
    assert(d3d11->dxgiFactory);
    IDXGIFactory2_Release(d3d11->dxgiFactory);
    d3d11->dxgiFactory = 0;
    
    IDXGIDebug *dxgiDebug = 0;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, &IID_IDXGIDebug, (void**)&dxgiDebug)))
    {
        IDXGIDebug_ReportLiveObjects(dxgiDebug, DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        IDXGIDebug_Release(dxgiDebug);
    }
}

bool
d3d11_create(D3D11Context *d3d11)
{
    UINT flags = D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    if (SUCCEEDED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE,
                                    0, flags, levels, ARRAYSIZE(levels),
                                    D3D11_SDK_VERSION,
                                    &d3d11->device, 0,
                                    &d3d11->context)))
    {
        ID3D11InfoQueue *info;
        ID3D11Device_QueryInterface(d3d11->device,
                                    &IID_ID3D11InfoQueue,
                                    (void**)&info);
        
        ID3D11InfoQueue_SetBreakOnSeverity(info,
                                           D3D11_MESSAGE_SEVERITY_CORRUPTION,
                                           TRUE);
        
        ID3D11InfoQueue_SetBreakOnSeverity(info,
                                           D3D11_MESSAGE_SEVERITY_ERROR,
                                           TRUE);
        ID3D11InfoQueue_Release(info);
        
        IDXGIInfoQueue *dxgiInfo;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, &IID_IDXGIInfoQueue,
                                             (void**)&dxgiInfo)))
        {
            IDXGIInfoQueue_SetBreakOnSeverity(dxgiInfo,
                                              DXGI_DEBUG_ALL,
                                              DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION,
                                              TRUE);
            
            IDXGIInfoQueue_SetBreakOnSeverity(dxgiInfo,
                                              DXGI_DEBUG_ALL,
                                              DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR,
                                              TRUE);
            
            IDXGIInfoQueue_Release(dxgiInfo);
            
            if (SUCCEEDED(ID3D11Device_QueryInterface(d3d11->device,
                                                      &IID_IDXGIDevice,
                                                      (void**)&d3d11->dxgiDevice)))
            {
                if (SUCCEEDED(IDXGIDevice_GetAdapter(d3d11->dxgiDevice,
                                                     &d3d11->dxgiAdapter)))
                {
                    if (SUCCEEDED(IDXGIAdapter_GetParent(d3d11->dxgiAdapter,
                                                         &IID_IDXGIFactory2,
                                                         (void**)&d3d11->dxgiFactory)))
                    {
                        return true;
                    }
                    else
                    {
                        print("Failed to get DXGI Factory.\n");
                    }
                }
                else
                {
                    print("Failed to get DXGI Adapter.\n");
                }
            }
            else
            {
                print("Failed to get DXGI Device.\n");
            }
        }
        else
        {
            print("Failed to enable DXGI Debug Messages.\n");
        }
    }
    
    return false;
}

bool
d3d11_create_window(int width, int height, char *title,
                    WNDPROC wndproc,
                    D3D11Window *destWindow)
{
    HINSTANCE instance = GetModuleHandle(0);
    
    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = instance;
    wc.lpszClassName = "D3D11WndClass";
    
    if (RegisterClassExA(&wc))
    {
        destWindow->wndClass = wc;
        destWindow->width = width;
        destWindow->height = height;
        
        DWORD exStyle = WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP;
        DWORD style = WS_OVERLAPPEDWINDOW;
        
        RECT rect = { 0, 0, width, height };
        AdjustWindowRectEx(&rect, style, FALSE, exStyle);
        width = rect.right - rect.left;
        height = rect.bottom - rect.top;
        
        destWindow->handle = CreateWindowExA(exStyle,
                                             wc.lpszClassName,
                                             title, style,
                                             CW_USEDEFAULT, CW_USEDEFAULT,
                                             width, height,
                                             NULL, NULL, instance, NULL);
        
        if (destWindow->handle)
        {
            return true;
        }
        else
        {
            print("Failed to create window");
        }
    }
    else
    {
        print("Failed to register window class");
    }
    
    return false;
}

void
d3d11_swapchain_destroy(IDXGISwapChain1 **swapChain)
{
    assert(*swapChain);
    IDXGISwapChain1_Release(*swapChain);
    swapChain = 0;
}

void
d3d11_swapchain_create(D3D11Context *d3d11, HWND window,
                       IDXGISwapChain1 **destSwapChain)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = (DXGI_SAMPLE_DESC){ 1, 0 };
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    
    IDXGIFactory2_CreateSwapChainForHwnd(d3d11->dxgiFactory,
                                         (IUnknown*)d3d11->device,
                                         window,
                                         &desc, 0, 0,
                                         destSwapChain);
    
    IDXGIFactory_MakeWindowAssociation(d3d11->dxgiFactory,
                                       window,
                                       DXGI_MWA_NO_ALT_ENTER);
}

void
d3d11_swapchain_resize(D3D11Context *d3d11,
                       D3D11Pipeline *pipeline,
                       UINT newWidth, UINT newHeight)
{
    if (pipeline->rtView)
    {
        ID3D11DeviceContext_ClearState(d3d11->context);
        ID3D11RenderTargetView_Release(pipeline->rtView);
        ID3D11DepthStencilView_Release(pipeline->dsView);
        pipeline->rtView = 0;
    }
    
    if (newWidth != 0 && newHeight != 0)
    {
        if (SUCCEEDED(IDXGISwapChain1_ResizeBuffers(pipeline->swapChain, 0,
                                                    newWidth, newHeight,
                                                    DXGI_FORMAT_UNKNOWN, 0)))
        {
            ID3D11Texture2D *backbuffer;
            IDXGISwapChain1_GetBuffer(pipeline->swapChain, 0,
                                      &IID_ID3D11Texture2D,
                                      (void**)&backbuffer);
            
            ID3D11Device_CreateRenderTargetView(d3d11->device,
                                                (ID3D11Resource*)backbuffer, 0,
                                                &pipeline->rtView);
            ID3D11Texture2D_Release(backbuffer);
            
            D3D11_TEXTURE2D_DESC desc = {0};
            desc.Width = newWidth;
            desc.Height = newHeight;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_D32_FLOAT;
            desc.SampleDesc = (DXGI_SAMPLE_DESC){ 1, 0 };
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            
            ID3D11Texture2D *depth;
            ID3D11Device_CreateTexture2D(d3d11->device, &desc, 0, &depth);
            ID3D11Device_CreateDepthStencilView(d3d11->device, (ID3D11Resource*)depth, 0,
                                                &pipeline->dsView);
            ID3D11Texture2D_Release(depth);
            
        }
        else
        {
            assert(!"Failed to resize swap chain!");
        }
    }
}

void
d3d11_buffer_destroy(ID3D11Buffer **buffer)
{
    assert(*buffer);
    ID3D11Buffer_Release(*buffer);
    buffer = 0;
}

void
d3d11_buffer_create(D3D11Context *d3d11,
                    D3D11_USAGE usage,
                    UINT bindFlags,
                    UINT cpuAccessFlags,
                    void *data, UINT dataSize,
                    ID3D11Buffer **destBuffer)
{
    D3D11_BUFFER_DESC desc = {0};
    desc.ByteWidth = dataSize;
    desc.Usage = usage;
    desc.BindFlags = bindFlags;
    desc.CPUAccessFlags = cpuAccessFlags;
    
    if (data)
    {
        D3D11_SUBRESOURCE_DATA initial = { .pSysMem = data };
        ID3D11Device_CreateBuffer(d3d11->device, &desc, &initial,
                                  destBuffer);
    }
    else
    {
        ID3D11Device_CreateBuffer(d3d11->device, &desc, 0,
                                  destBuffer);
    }
}

void
d3d11_shader_destroy(D3D11Shader *shader)
{
    assert(shader->vertex);
    ID3D11VertexShader_Release(shader->vertex);
    shader->vertex = 0;
    
    assert(shader->pixel);
    ID3D11PixelShader_Release(shader->pixel);
    shader->pixel = 0;
    
    assert(shader->layout);
    ID3D11InputLayout_Release(shader->layout);
    shader->layout = 0;
}

void
d3d11_shader_create(D3D11Context *d3d11,
                    D3D11_INPUT_ELEMENT_DESC layout[], UINT layoutCount,
                    void *data, UINT dataSize,
                    D3D11Shader *destShader)
{
    UINT flags = (D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
                  D3DCOMPILE_ENABLE_STRICTNESS |
                  D3DCOMPILE_WARNINGS_ARE_ERRORS |
                  D3DCOMPILE_DEBUG |
                  D3DCOMPILE_SKIP_OPTIMIZATION);
    
    ID3DBlob *error, *vblob;
    
    if (SUCCEEDED(D3DCompile(data, dataSize,
                             0, 0, 0,
                             "vs", "vs_5_0",
                             flags, 0,
                             &vblob, &error)))
    {
        ID3DBlob *pblob;
        if (SUCCEEDED(D3DCompile(data, dataSize,
                                 0, 0, 0,
                                 "ps", "ps_5_0",
                                 flags, 0,
                                 &pblob, &error)))
        {
            ID3D11Device_CreateVertexShader(d3d11->device,
                                            ID3D10Blob_GetBufferPointer(vblob),
                                            ID3D10Blob_GetBufferSize(vblob),
                                            0, &destShader->vertex);
            
            ID3D11Device_CreatePixelShader(d3d11->device,
                                           ID3D10Blob_GetBufferPointer(pblob),
                                           ID3D10Blob_GetBufferSize(pblob),
                                           0, &destShader->pixel);
            
            ID3D11Device_CreateInputLayout(d3d11->device,
                                           layout, layoutCount,
                                           ID3D10Blob_GetBufferPointer(vblob),
                                           ID3D10Blob_GetBufferSize(vblob),
                                           &destShader->layout);
            
            ID3D10Blob_Release(pblob);
        }
        else
        {
            print(ID3D10Blob_GetBufferPointer(error));
        }
        
        ID3D10Blob_Release(vblob);
    }
    else
    {
        print(ID3D10Blob_GetBufferPointer(error));
    }
}

void
d3d11_texture_destroy(D3D11Texture *texture)
{
    assert(texture->handle);
    ID3D11Texture2D_Release(texture->handle);
    texture->handle = 0;
    
    assert(texture->view);
    ID3D11ShaderResourceView_Release(texture->view);
    texture->view = 0;
}

void
d3d11_texture_create(D3D11Context *d3d11,
                     UINT width, UINT height, void *data,
                     D3D11Texture *destTexture)
{
    D3D11_TEXTURE2D_DESC desc =
    {
        .Width = width,
        .Height = height,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc = { 1, 0 },
        .Usage = D3D11_USAGE_IMMUTABLE,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
    };
    
    D3D11_SUBRESOURCE_DATA initial =
    {
        .pSysMem = data,
        .SysMemPitch = width * sizeof(unsigned int),
    };
    
    ID3D11Device_CreateTexture2D(d3d11->device,
                                 &desc,
                                 &initial,
                                 &destTexture->handle);
    
    ID3D11Device_CreateShaderResourceView(d3d11->device,
                                          (ID3D11Resource *)destTexture->handle,
                                          0,
                                          &destTexture->view);
}

void
d3d11_pipeline_destroy(D3D11Pipeline *pipeline)
{
    d3d11_swapchain_destroy(&pipeline->swapChain);
    
    assert(pipeline->sampler);
    ID3D11SamplerState_Release(pipeline->sampler);
    pipeline->sampler = 0;
    
    assert(pipeline->blend);
    ID3D11BlendState_Release(pipeline->blend);
    pipeline->blend = 0;
    
    assert(pipeline->rasterizer);
    ID3D11RasterizerState_Release(pipeline->rasterizer);
    pipeline->rasterizer = 0;
    
    assert(pipeline->ds);
    ID3D11DepthStencilState_Release(pipeline->ds);
    pipeline->ds = 0;
    
    assert(pipeline->rtView);
    ID3D11RenderTargetView_Release(pipeline->rtView);
    pipeline->rtView = 0;
    
    assert(pipeline->dsView);
    ID3D11DepthStencilView_Release(pipeline->dsView);
    pipeline->dsView = 0;
}

void
d3d11_pipeline_create(D3D11Context *d3d11,
                      HWND window,
                      D3D11Pipeline *destPipeline)
{
    d3d11_swapchain_create(d3d11, window,
                           &destPipeline->swapChain);
    
    D3D11_SAMPLER_DESC sampler = {0};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.MaxAnisotropy = 1;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11Device_CreateSamplerState(d3d11->device, &sampler,
                                    &destPipeline->sampler);
    
    D3D11_RENDER_TARGET_BLEND_DESC blend1 = {0};
    blend1.BlendEnable = TRUE;
    blend1.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend1.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend1.BlendOp = D3D11_BLEND_OP_ADD;
    blend1.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
    blend1.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend1.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend1.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_BLEND_DESC blend = {0};
    blend.RenderTarget[0] = blend1;
    ID3D11Device_CreateBlendState(d3d11->device, &blend,
                                  &destPipeline->blend);
    
    D3D11_RASTERIZER_DESC raster = {0};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    ID3D11Device_CreateRasterizerState(d3d11->device, &raster,
                                       &destPipeline->rasterizer);
    
    D3D11_DEPTH_STENCIL_DESC ds = {0};
    ds.DepthEnable = FALSE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    ds.StencilEnable = FALSE;
    ds.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    ds.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    ID3D11Device_CreateDepthStencilState(d3d11->device, &ds,
                                         &destPipeline->ds);
}

void
d3d11_render_pass(D3D11Context *d3d11,
                  D3D11Pipeline *pipeline,
                  D3D11Shader *shader,
                  D3D11Texture *texture,
                  ID3D11Buffer *vertexBuffer,
                  ID3D11Buffer *indexBuffer,
                  unsigned short indexCount,
                  ID3D11Buffer *uniformBuffer,
                  D3D11_VIEWPORT viewport,
                  FLOAT color[4],
                  float matrix[16])
{
    ID3D11DeviceContext_ClearRenderTargetView(d3d11->context, pipeline->rtView, color);
    ID3D11DeviceContext_ClearDepthStencilView(d3d11->context, pipeline->dsView,
                                              D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11DeviceContext_Map(d3d11->context, (ID3D11Resource*)uniformBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, matrix, 16 * sizeof(float));
    ID3D11DeviceContext_Unmap(d3d11->context, (ID3D11Resource*)uniformBuffer, 0);
    
    // Input Assembler
    ID3D11DeviceContext_IASetInputLayout(d3d11->context, shader->layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(d3d11->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(D3D11Vertex);
    UINT offset = 0;
    ID3D11DeviceContext_IASetVertexBuffers(d3d11->context, 0, 1, &vertexBuffer, &stride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(d3d11->context, indexBuffer, DXGI_FORMAT_R16_UINT, 0);
    
    // Vertex Shader
    ID3D11DeviceContext_VSSetConstantBuffers(d3d11->context, 0, 1, &uniformBuffer);
    ID3D11DeviceContext_VSSetShader(d3d11->context, shader->vertex, 0, 0);
    
    // Rasterizer Stage
    ID3D11DeviceContext_RSSetViewports(d3d11->context, 1, &viewport);
    ID3D11DeviceContext_RSSetState(d3d11->context, pipeline->rasterizer);
    
    // Pixel Shader
    ID3D11DeviceContext_PSSetSamplers(d3d11->context, 0, 1, &pipeline->sampler);
    ID3D11DeviceContext_PSSetShaderResources(d3d11->context, 0, 1, &texture->view);
    ID3D11DeviceContext_PSSetShader(d3d11->context, shader->pixel, 0, 0);
    
    // Output Merger
    ID3D11DeviceContext_OMSetBlendState(d3d11->context, pipeline->blend, 0, ~0U);
    ID3D11DeviceContext_OMSetDepthStencilState(d3d11->context, pipeline->ds, 0);
    ID3D11DeviceContext_OMSetRenderTargets(d3d11->context, 1, &pipeline->rtView, pipeline->dsView);
    
    // Draw call
    ID3D11DeviceContext_DrawIndexed(d3d11->context, indexCount, 0, 0);
}

static LRESULT CALLBACK
d3d11_wndproc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;
    
    AppState *app = (AppState *)GetWindowLongPtr(window, GWLP_USERDATA);
    
    switch (msg)
    {
        case WM_CLOSE:
        case WM_DESTROY:
        {
            app->running = false;
        } break;
        
        case WM_SIZE:
        {
            app->window.width = LOWORD(lParam);
            app->window.height = HIWORD(lParam);
            d3d11_swapchain_resize(&app->d3d11, &app->pipeline,
                                   app->window.width, app->window.height);
            
        } break;
        
        default:
        {
            result = DefWindowProcA(window, msg, wParam, lParam);
        } break;
    }
    
    return result;
}

void
app_destroy(AppState *app)
{
    d3d11_shader_destroy(&app->shader);
    d3d11_texture_destroy(&app->texture);
    d3d11_buffer_destroy(&app->vertexBuffer);
    d3d11_buffer_destroy(&app->indexBuffer);
    d3d11_buffer_destroy(&app->uniformBuffer);
    d3d11_pipeline_destroy(&app->pipeline);
    d3d11_destroy(&app->d3d11);
}

void
app_init(AppState *app)
{
    if (d3d11_create(&app->d3d11))
    {
        if (d3d11_create_window(800, 600, "My Window", d3d11_wndproc, &app->window))
        {
            SetWindowLongPtr(app->window.handle, GWLP_USERDATA, (LONG_PTR)app);
            
            d3d11_pipeline_create(&app->d3d11, app->window.handle,
                                  &app->pipeline);
            
            // Vertex Buffer
            D3D11Vertex vertices[] =
            {
                { { -0.5f, -0.5f }, { 0.0f, 0.0f }, { 1, 0, 0 } },
                { { +0.5f, -0.5f }, { 3.0f, 0.0f }, { 0, 1, 0 } },
                { { +0.5f, +0.5f }, { 3.0f, 3.0f }, { 0, 0, 1 } },
                { { -0.5f, +0.5f }, { 0.0f, 3.0f }, { 1, 1, 1 } },
            };
            
            d3d11_buffer_create(&app->d3d11,
                                D3D11_USAGE_IMMUTABLE,
                                D3D11_BIND_VERTEX_BUFFER, 0,
                                vertices, sizeof(vertices),
                                &app->vertexBuffer);
            
            // Index Buffer
            unsigned short indices[] = { 0, 1, 2, 2, 3, 0 };
            d3d11_buffer_create(&app->d3d11,
                                D3D11_USAGE_IMMUTABLE,
                                D3D11_BIND_INDEX_BUFFER, 0,
                                indices, sizeof(indices),
                                &app->indexBuffer);
            
            app->indexCount = 6;
            
            // Uniform Buffer
            d3d11_buffer_create(&app->d3d11,
                                D3D11_USAGE_DYNAMIC,
                                D3D11_BIND_CONSTANT_BUFFER,
                                D3D11_CPU_ACCESS_WRITE, 0,
                                4 * 4 * sizeof(float),
                                &app->uniformBuffer);
            
            // Shader
            D3D11_INPUT_ELEMENT_DESC layout[] =
            {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(D3D11Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(D3D11Vertex, uv),       D3D11_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(D3D11Vertex, color),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };
            
            char hlsl[] =
                "struct VS_INPUT                                            \n"
                "{                                                          \n"
                "    float2 p     : POSITION;                               \n"
                "    float2 uv    : TEXCOORD;                               \n"
                "    float3 color : COLOR;                                  \n"
                "};                                                         \n"
                "                                                           \n"
                "struct PS_INPUT                                            \n"
                "{                                                          \n"
                "    float4 p     : SV_POSITION;                            \n"
                "    float2 uv    : TEXCOORD;                               \n"
                "    float4 color : COLOR;                                  \n"
                "};                                                         \n"
                "                                                           \n"
                "cbuffer cbuffer0 : register(b0)                            \n"
                "{                                                          \n"
                "    row_major float4x4 uTransform;                         \n"
                "}                                                          \n"
                "                                                           \n"
                "sampler sampler0 : register(s0);                           \n"
                "                                                           \n"
                "Texture2D<float4> texture0 : register(t0);                 \n"
                "                                                           \n"
                "PS_INPUT vs(VS_INPUT input)                                \n"
                "{                                                          \n"
                "    PS_INPUT output;                                       \n"
                "    output.p  = mul(uTransform, float4(input.p, 0, 1));    \n"
                "    output.uv = input.uv;                                  \n"
                "    output.color = float4(input.color, 1);                 \n"
                "    return output;                                         \n"
                "}                                                          \n"
                "                                                           \n"
                "float4 ps(PS_INPUT input) : SV_TARGET                      \n"
                "{                                                          \n"
                "    float4 tex = texture0.Sample(sampler0, input.uv);      \n"
                "    return input.color * tex;                              \n"
                "}                                                          \n";
            
            d3d11_shader_create(&app->d3d11,
                                layout, ARRAYSIZE(layout),
                                hlsl, sizeof(hlsl),
                                &app->shader);
            
            // Texture
            unsigned int pixels[] =
            {
                0x00000000, 0xffffffff,
                0xffffffff, 0x00000000,
            };
            
            d3d11_texture_create(&app->d3d11, 2, 2, pixels, &app->texture);
            
            app->running = true;
            
            ShowWindow(app->window.handle, SW_SHOWDEFAULT);
        }
    }
}

int WINAPI
WinMain(HINSTANCE instance, HINSTANCE previnstance, LPSTR cmdline, int cmdshow)
{
    AppState app = {0};
    app_init(&app);
    
    LARGE_INTEGER freq, c1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c1);
    float angle = 0;
    
    while (app.running)
    {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        if (app.pipeline.rtView)
        {
            LARGE_INTEGER c2;
            QueryPerformanceCounter(&c2);
            float delta = (float)((double)(c2.QuadPart - c1.QuadPart) / freq.QuadPart);
            c1 = c2;
            
            D3D11_VIEWPORT viewport =
            {
                .TopLeftX = 0,
                .TopLeftY = 0,
                .Width = (FLOAT)app.window.width,
                .Height = (FLOAT)app.window.height,
                .MinDepth = 0,
                .MaxDepth = 1,
            };
            
            FLOAT color[] = { 0.392f, 0.584f, 0.929f, 1.f };
            
            angle += delta * 2.0f * (float)M_PI / 20.0f;
            angle = fmodf(angle, 2.0f * (float)M_PI);
            
            float aspect = (float)app.window.height / app.window.width;
            float a = cosf(angle) * aspect;
            float b = sinf(angle) * aspect;
            float c = -sinf(angle);
            float d = cosf(angle);
            
            // NOTE: Row-Major notation
            float matrix[16] =
            {
                a, b, 0, 0,
                c, d, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1,
            };
            
            d3d11_render_pass(&app.d3d11, &app.pipeline, &app.shader, &app.texture,
                              app.vertexBuffer, app.indexBuffer, app.indexCount,
                              app.uniformBuffer, viewport, color, matrix);
        }
        
        BOOL vsync = TRUE;
        HRESULT hr = IDXGISwapChain1_Present(app.pipeline.swapChain, vsync ? 1 : 0, 0);
        if (hr == DXGI_STATUS_OCCLUDED)
        {
            if (vsync)
            {
                Sleep(10);
            }
        }
        else if (FAILED(hr))
        {
            assert(!"Failed to present swap chain! Device lost?");
        }
    }
    
    app_destroy(&app);
}
