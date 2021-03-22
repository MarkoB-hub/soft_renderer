#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <algorithm>
#include <windows.h>
#include <wingdi.h>
#include <stdio.h>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>


#define internal static 
#define global_variable static 
#define local_persist static

#include <stdint.h>
#include <stddef.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

typedef i8 b8;
typedef size_t sizet;

#ifdef DEBUG

inline internal void
DEBUG_print(char* text)
{
    OutputDebugString(text);
}

global_variable char DEBUG_string_buffer[512];

#define DEBUG_PRINT(msg, ...) \
    sprintf_s(DEBUG_string_buffer, msg, __VA_ARGS__); \
    DEBUG_print(DEBUG_string_buffer);

#else

#define DEBUG_PRINT(msg, ...) 

#endif 


#include "math.h"
#include "model.cpp"

#define BYTES_PER_PIXEL 4
#define BIG_F32 214748364.0f
global_variable i32 Bitmap_Width = 800;
global_variable i32 Bitmap_Height = 600;
global_variable i32 Depth = 600;
global_variable BITMAPINFO Bitmap_Info;
global_variable HANDLE Semaphore_Handle;

struct Color
{
    u8 r, g, b;
};

struct Renderer
{
    mat4 projection;
    mat4 view_port;
    mat4 view;
    Camera camera;
    vec3 light_direction;
    f32* z_buffer;
    void* framebuffer;
};

internal void 
swap(i32* x, i32* y)
{
    i32 temp = *x;
    *x = *y;
    *y = temp;
}

internal void 
swap(f32* x, f32* y)
{
    f32 temp = *x;
    *x = *y;
    *y = temp;
}

internal void 
swap(vec3* x, vec3* y)
{
    vec3 temp = *x;
    *x = *y;
    *y = temp;
}


//TODO: make it work with window
/*
static void 
draw_line(int x0, int y0, int x1, int y1, TGAImage* image, TGAColor color)
{
    bool steep = false;
    if (abs(x0-x1) < abs(y0-y1))
    {
        swap(&x0, &y0);
        swap(&x1, &y1);
        steep = true;
    }

    if (x0 > x1)
    {
        swap(&x0, &x1);
        swap(&y0, &y1);
    }

    for (int x = x0; x <= x1; ++x)
    {
        f32 t = (x-x0)/(f32)(x1-x0);
        int y = (int)(y0*(1.0f-t) + y1*t);
        if (steep)
        {
            image->set(y, x, color);
        }
        else
        {
            image->set(x, y, color);
        }
    }
}
*/



internal void
bitmap_set_pixel(u32* framebuffer, i32 x, i32 y, Color color)
{
    u32* pixel = framebuffer + (y * Bitmap_Width + x);
    u8* channel = (u8*)pixel;
    *channel = color.b;
    channel++;
    *channel = color.g;
    channel++;
    *channel = color.r;
    channel++;
}

/*
internal void
bitmap_draw_rect(i32 px, i32 py, i32 w, i32 h)
{
    for (sizet y = py; y < h; ++y)
    {
        for (sizet x = px; x < w; ++x)
        {
            u32* pixel = (u32*)Bitmap_Memory + (y * Bitmap_Width + x);
        }
    }
}
*/


internal void
draw_triangle(void* framebuffer, vec3 v[3], vec3 uv[3], vec3 light_intensity_coord, u8* texture, f32 *zbuffer)
{
    if (v[0].y > v[1].y) { swap(&v[0], &v[1]); }
    if (v[0].y > v[2].y) { swap(&v[0], &v[2]); }
    if (v[1].y > v[2].y) { swap(&v[1], &v[2]); }
    if (uv[0].y > uv[1].y) { swap(&uv[0], &uv[1]); }
    if (uv[0].y > uv[2].y) { swap(&uv[0], &uv[2]); }
    if (uv[1].y > uv[2].y) { swap(&uv[1], &uv[2]); }

    Box box = triangle_bounding_box(v);

    for (i32 y = (i32)box.y1; y <= (i32)box.y2; ++y)
    {
        for (i32 x = (i32)box.x1; x <= (i32)box.x2; ++x)
        {
            vec3 bc_coord = barycentric_coord(v, {(f32)x, (f32)y});
            if (bc_coord.x<0.0f || bc_coord.y<0.0f || bc_coord.z<0.0f) 
                continue; 

            f32 z = 0.0f;
            f32 light_intensity = 0.0f;

            for (i32 i = 0; i < 3; ++i)
            {
                z += v[i].z * bc_coord[i];
            }
            light_intensity = vec3_dot(light_intensity_coord, bc_coord);

            if (z > zbuffer[y * Bitmap_Width + x])
            {
                zbuffer[y * Bitmap_Width + x] = z;
                Color color = {};
                if (light_intensity > 0)
                {
                    color.r = 255 * light_intensity;
                    color.g = 255 * light_intensity;
                    color.b = 255 * light_intensity;
                }
                bitmap_set_pixel((u32*)framebuffer, x, y, color);
            }
        }
    }
}





internal void
clear_z_buffer(Renderer* ren)
{
    for (i32 y = 0; y < Bitmap_Height; ++y)
    {
        for (i32 x = 0; x < Bitmap_Width; ++x)
        {
            ren->z_buffer[y * Bitmap_Width + x] = -BIG_F32;
        }
    }
}

internal void
win32_resize_bitmap(i32 w, i32 h, Renderer* ren)
{
    if (ren->framebuffer)
    {
        free(ren->framebuffer);
        free(ren->z_buffer);
        ren->framebuffer = NULL;
    }
    Bitmap_Width = w;
    Bitmap_Height = h;

    Bitmap_Info.bmiHeader.biSize = sizeof(Bitmap_Info.bmiHeader);
    Bitmap_Info.bmiHeader.biWidth = Bitmap_Width;
    Bitmap_Info.bmiHeader.biHeight = Bitmap_Height;
    Bitmap_Info.bmiHeader.biPlanes = 1;
    Bitmap_Info.bmiHeader.biBitCount = 32;
    Bitmap_Info.bmiHeader.biCompression = BI_RGB;

    ren->framebuffer = calloc(w * h * BYTES_PER_PIXEL, sizeof(u8));
    ren->z_buffer = (f32*)malloc(Bitmap_Width * Bitmap_Height * sizeof(f32));
    clear_z_buffer(ren);
}

internal void
clear_screen(Renderer* ren)
{
    memset(ren->framebuffer, 50, Bitmap_Width * Bitmap_Height * BYTES_PER_PIXEL * sizeof(u8));
    clear_z_buffer(ren);
}

LRESULT CALLBACK 
win32_window_callback(
  _In_ HWND   window_handle,
  _In_ UINT   message,
  _In_ WPARAM w_param,
  _In_ LPARAM l_param
) 
{
    LRESULT result = 0;

    switch(message)
    {
        case WM_SIZE:
        {
            /*
            RECT screen_rect;
            GetClientRect(window_handle, &screen_rect);
            i32 w = screen_rect.right - screen_rect.left;
            i32 h = screen_rect.bottom - screen_rect.top;
            win32_resize_bitmap(w, h);
            */
        } break;
        default:
        {
            result = DefWindowProc(window_handle, message, w_param, l_param);
        } break;
    }
    return result;
}

global_variable LARGE_INTEGER global_pref_count_freq;

internal LARGE_INTEGER
win32_get_preformance_counter()
{
    LARGE_INTEGER out = {};
    QueryPerformanceCounter(&out);

    return out;
}

internal f32 
win32_get_elapsed_seconds(LARGE_INTEGER start, LARGE_INTEGER end)
{
    i64 elapsed_sec = end.QuadPart - start.QuadPart;
    f32 out = (f32)elapsed_sec / (f32)global_pref_count_freq.QuadPart;

    return out;
}

internal void
draw_fps(HDC device_context, f32 time)
{
    RECT text_rect;
    text_rect.left = 0;
    text_rect.top = 0;
    text_rect.right = 200;
    text_rect.bottom = 200;

    SetTextColor(device_context, RGB(0, 0, 0));
    char* test = "%i fps";
    char text[20];
    sprintf_s(text, test, (i32)(1.0f/time)); 
    i32 success = DrawText(device_context,
                           text,
                           -1,
                           &text_rect,
                           DT_CENTER);

    if (!success)
    {
        DEBUG_PRINT("Failed to draw text\n");
    }

}


#define ARRAY_COUNT(a) (sizeof(a) / sizeof(a[0]))

struct TriangleInfo
{
    vec3 world_coord[3];
    vec3 uv_coord[3];
    vec3 light_intensity_coord;
    f32* z_buffer;
    void* framebuffer;
};

#define MAX_ENTRY_COUNT 5120
global_variable u32 volatile entry_completion_count;
global_variable u32 volatile next_entry_todo;
global_variable u32 volatile entry_count;
TriangleInfo Entries[MAX_ENTRY_COUNT];

#define CompletePastWritesBeforeFutureWrites _mm_sfence()
#define CompletePastReadsBeforeFutureReads 


internal void
push_triangle(TriangleInfo& triangle_info)
{

    TriangleInfo *Entry = Entries + (entry_count % MAX_ENTRY_COUNT);
    *Entry = triangle_info;

    CompletePastWritesBeforeFutureWrites;
    
    ++entry_count;

    ReleaseSemaphore(Semaphore_Handle, 1, 0);
}

struct Win32ThreadInfo
{
    i32 thread_index;
};

DWORD WINAPI
thread_proc(LPVOID lpParameter)
{
    Win32ThreadInfo *thread_info = (Win32ThreadInfo *)lpParameter;
    
    for(;;)
    {
        if(next_entry_todo < entry_count)
        {
            i32 entry_index = InterlockedIncrement((LONG volatile *)&next_entry_todo) - 1;
            CompletePastReadsBeforeFutureReads;

            // NOTE(marko): looping array
            // TODO(marko): fix array looping causes some triangles not to render
            TriangleInfo *entry = Entries + (entry_index % MAX_ENTRY_COUNT);

            draw_triangle(entry->framebuffer,
                          entry->world_coord, 
                          entry->uv_coord, 
                          entry->light_intensity_coord,
                          NULL, 
                          entry->z_buffer);

            InterlockedIncrement((LONG volatile *)&entry_completion_count);
        }
        else
        {
            WaitForSingleObjectEx(Semaphore_Handle, INFINITE, FALSE);
        }
    }
}

internal void
draw_model(Renderer* ren, Model* model)
{
    for (i32 i = 0; i < model->model_info.face_count; ++i)
    {
        Face face = model->faces[i];
        TriangleInfo tri;
        vec3 vertex_normals[3];

        for (i32 j = 0; j < 3; ++j)
        {
            vec3 vertex = vertex_from_face(face, model->vertices, j);
            vec3 vertex_normal = vertex_from_face(face, model->vertex_normals, j);


            vec4 vec4d = {vertex.x, vertex.y, vertex.z, 1.0f};
            vec4 temp = ren->view_port * ren->projection * ren->view * vec4d;
            tri.world_coord[j] = {temp.x/temp.w, temp.y/temp.w, temp.z/temp.w};
            tri.uv_coord[j] = texture_uv_from_face(face, model->texture_uvs, j);
            vertex_normals[j] = vertex_normal;
        }
        if (tri.world_coord[0].y > tri.world_coord[1].y) {
            swap(&tri.world_coord[0], &tri.world_coord[1]); 
            swap(&vertex_normals[0], &vertex_normals[1]);
        }
        if (tri.world_coord[0].y > tri.world_coord[2].y) { 
            swap(&tri.world_coord[0], &tri.world_coord[2]); 
            swap(&vertex_normals[0], &vertex_normals[2]);
        }
        if (tri.world_coord[1].y > tri.world_coord[2].y) { 
            swap(&tri.world_coord[1], &tri.world_coord[2]); 
            swap(&vertex_normals[1], &vertex_normals[2]);
        }
        for (i32 k = 0; k < 3; ++k)
        {
            tri.light_intensity_coord[k] = max(0.0f, vec3_dot(vertex_normals[k], ren->light_direction));
        }

        tri.z_buffer = ren->z_buffer;
        tri.framebuffer = ren->framebuffer;

#ifdef THREADED
        push_triangle(tri);
#else
        draw_triangle(tri.framebuffer,
                      tri.world_coord, 
                      tri.uv_coord, 
                      tri.light_intensity_coord,
                      NULL, 
                      tri.z_buffer);
#endif
    }

#ifdef THREADED
    while(entry_count != entry_completion_count);

    entry_completion_count = 0;
    next_entry_todo = 0;
    entry_count = 0;
#endif

}
i32
WinMain(HINSTANCE hinstance,
        HINSTANCE prev_hinstance,
        LPSTR cmd_line,
        i32 show_code)
{
#ifdef THREADED
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    u32 thread_count = sys_info.dwNumberOfProcessors;

    Win32ThreadInfo* thread_info = (Win32ThreadInfo*)malloc(sizeof(Win32ThreadInfo) * thread_count);

    u32 initial_count = 0;
    Semaphore_Handle = CreateSemaphoreEx(0,
                                         initial_count,
                                         thread_count,
                                         0, 0, SEMAPHORE_ALL_ACCESS);
    for(u32 thread_index = 0;
        thread_index < thread_count;
        ++thread_index)
    {
        Win32ThreadInfo *info = thread_info + thread_index;
        info->thread_index = thread_index;
        
        DWORD id;
        HANDLE thread_handle = CreateThread(0, 0, thread_proc, info, 0, &id);
        CloseHandle(thread_handle);
    }
#endif

    
    WNDCLASS window_class = {};
    window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = win32_window_callback;
    window_class.hInstance = hinstance;
    window_class.lpszClassName = "Win32WindowClass";

    RegisterClass(&window_class);
    HWND window_handle = CreateWindowEx(
            0,
            window_class.lpszClassName,
            "Game",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            0,
            0,
            hinstance, 
            0); 



    Renderer renderer = {};
    renderer.camera.position = {0.0f, 0.0f, 3.0f};
    renderer.camera.direction = {0.0f, 0.0f, 1.0f};
    renderer.camera.up = {0.0f, 1.0f, 0.0f};

    Model head_model = load_model("../head.obj");


    win32_resize_bitmap(Bitmap_Width, Bitmap_Height, &renderer);
    b8 running = true;

    QueryPerformanceFrequency(&global_pref_count_freq);
    f32 target_fps = 10.0f;
    f32 target_sec_per_frame = 1.0f / target_fps;
    LARGE_INTEGER last_counter = win32_get_preformance_counter();

    while(running)
    {
        MSG message;
        while(PeekMessage(&message, 0, 0, 0, PM_REMOVE)) 
        {

            switch(message.message)
            {
                case WM_CLOSE:
                case WM_DESTROY:
                {
                    running = false;
                } break;
                case WM_KEYUP:
                case WM_KEYDOWN:
                {

                    b8 is_down = ((message.lParam & (1 << 31)) == 0);
                    u32 vk_code = (u32)message.wParam;

                    if (is_down)
                    {
                        switch(vk_code)
                        {
                            case 'A':
                            {
                                renderer.camera.position.x -= 0.1f;
                            } break;
                            case 'D':
                            {
                                renderer.camera.position.x += 0.1f;
                            } break;
                            case 'W':
                            {
                                renderer.camera.position.z -= 0.1f;
                            } break;
                            case 'S':
                            {
                                renderer.camera.position.z += 0.1f;
                            } break;
                        }
                    }
                } break;
                default:
                {
                    TranslateMessage(&message);
                    DispatchMessageA(&message);
                } break;
            }
        }
        LARGE_INTEGER end_count = win32_get_preformance_counter();
        f32 delta_time = win32_get_elapsed_seconds(last_counter, end_count);
        last_counter = win32_get_preformance_counter();

        renderer.view_port = mat4_view_port(0, 0, Bitmap_Width, Bitmap_Height, Depth/2);
        f32 ar = Bitmap_Width/(f32)Bitmap_Height;
        renderer.projection = {
            1.0f/ar, 0.0f,  0.0f,   0.0f,
            0.0f, 1.0f,  0.0f,   0.0f,
            0.0f, 0.0f,  1.0f,   0.0f,
            0.0f, 0.0f, -1.0f/3, 1.0f,
        };
        //renderer.view = mat4_look_at(renderer.camera.position, {}, renderer.camera.up);
        renderer.view = camera_transform(&renderer.camera);
        renderer.light_direction = {0.0f, 0.0f, 1.0f};

        RECT screen_rect;
        GetClientRect(window_handle, &screen_rect);
        i32 w = screen_rect.right - screen_rect.left;
        i32 h = screen_rect.bottom - screen_rect.top;

        if (w != Bitmap_Width || h != Bitmap_Height)
        {
            win32_resize_bitmap(w, h, &renderer);
        }

        HDC device_context = GetDC(window_handle);

        clear_screen(&renderer);
        draw_model(&renderer, &head_model);

        StretchDIBits(device_context,
                     0, 0, Bitmap_Width, Bitmap_Height,
                     0, 0, w, h,
                     renderer.framebuffer,
                     &Bitmap_Info,
                     DIB_RGB_COLORS,
                     SRCCOPY);
        draw_fps(device_context, delta_time);


    }
	return 0;
}

