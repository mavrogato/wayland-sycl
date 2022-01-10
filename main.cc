
#include <concepts>
#include <iostream>
#include <memory>
#include <cassert>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <CL/sycl.hpp>

/////////////////////////////////////////////////////////////////////////////
inline namespace wayland_client_helper
{

template <class WL_CLIENT> constexpr void* wl_interface_ptr = nullptr;

#define INTERN_WL_INTERFACE(wl_client)                                  \
    template <> constexpr wl_interface const* wl_interface_ptr<wl_client> = &wl_client##_interface;
INTERN_WL_INTERFACE(wl_display);
INTERN_WL_INTERFACE(wl_registry);
INTERN_WL_INTERFACE(wl_compositor);
INTERN_WL_INTERFACE(wl_shell);
INTERN_WL_INTERFACE(wl_seat);
INTERN_WL_INTERFACE(wl_keyboard);
INTERN_WL_INTERFACE(wl_pointer);
INTERN_WL_INTERFACE(wl_shm);
INTERN_WL_INTERFACE(wl_surface);
INTERN_WL_INTERFACE(wl_shell_surface);
INTERN_WL_INTERFACE(wl_buffer);
INTERN_WL_INTERFACE(wl_shm_pool);

template <class T>
concept wl_client = std::same_as<decltype (wl_interface_ptr<T>), wl_interface const *const>;

template <wl_client T, class Ch>
auto& operator << (std::basic_ostream<Ch>& output, T const* ptr) {
    return output << static_cast<void const*>(ptr)
                  << '['
                  << wl_interface_ptr<T>->name
                  << ']';
}

template <wl_client T>
auto attach_unique(T* ptr) noexcept {
    assert(ptr);
    static void (*deleter)(T*) = nullptr;
    if      constexpr (&wl_display_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_display_disconnect;
                      }
    else if constexpr (&wl_registry_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_registry_destroy;
                      }
    else if constexpr (&wl_compositor_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_compositor_destroy;
                      }
    else if constexpr (&wl_shell_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_shell_destroy;
                      }
    else if constexpr (&wl_seat_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_seat_destroy;
                      }
    else if constexpr (&wl_pointer_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_pointer_release;
                      }
    else if constexpr (&wl_keyboard_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_keyboard_release;
                      }
    else if constexpr (&wl_shm_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_shm_destroy;
                      }
    else if constexpr (&wl_surface_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_surface_destroy;
                      }
    else if constexpr (&wl_shell_surface_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_shell_surface_destroy;
                      }
    else if constexpr (&wl_buffer_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_buffer_destroy;
                      }
    else if constexpr (&wl_shm_pool_interface == wl_interface_ptr<T>)
                      {
                          deleter = wl_shm_pool_destroy;
                      }
    auto deleter_closure = [](T* ptr) {
        std::cout << wl_interface_ptr<T>->name << " deleting..." << std::endl;
        deleter(ptr);
    };
    return std::unique_ptr<T, decltype (deleter_closure)>(ptr, deleter_closure);
}

template <size_t N>
void register_global_callback(auto...) noexcept { }

template <size_t N, wl_client First, wl_client... Rest>
void register_global_callback(auto... args) noexcept
{
    auto [data, registry, id, interface, version] = std::tuple(args...);
    //!!! if constexpr (N == 0) { std::cout << interface << " (ver." << version << ") found."; }
    constexpr auto interface_ptr = wl_interface_ptr<First>;
    auto& ret = *(reinterpret_cast<First**>(data) + N);
    if (std::string_view(interface) == interface_ptr->name) {
        ret = (First*) wl_registry_bind(registry,
                                         id,
                                         interface_ptr,
                                         version);
        //!!! std::cout << "  ==> registered at " << ret;
    }
    else {
        register_global_callback<N-1, Rest...>(args...);
    }
    //!!! if constexpr (N == 0) { std::cout << std::endl; }
}

/////////////////////////////////////////////////////////////////////////////
namespace tentative_solution
{
template <class Tuple, size_t... I>
auto transform_each_impl(Tuple t, std::index_sequence<I...>) noexcept {
    return std::make_tuple(
        attach_unique(std::get<I>(t))...
    );
}
template <class... Args>
auto transform_each(std::tuple<Args...> const& t) noexcept {
    return transform_each_impl(t, std::make_index_sequence<sizeof... (Args)>{});
}
} // end of namespace tentative_solution

// Note: Still depends the reversed memory order of std::tuple
template <class... Args>
auto register_global(wl_display* display) noexcept {
    auto registry = attach_unique(wl_display_get_registry(display));
    std::tuple<Args*...> result;
    static constexpr wl_registry_listener listener {
        .global = register_global_callback<sizeof... (Args) -1, Args...>,
        .global_remove = [](auto...) noexcept { },
    };
    wl_registry_add_listener(registry.get(), &listener, &result);
    wl_display_dispatch(display);
    wl_display_roundtrip(display);
    return tentative_solution::transform_each(result);
}

} // end of namespace wayland_client_helper

/////////////////////////////////////////////////////////////////////////////
inline namespace egl_helper
{

template <class T, class D>
inline auto attach_unique(T* ptr, D del) {
    assert(ptr);
    return std::unique_ptr<T, D>(ptr, del);
}

inline auto attach_unique(EGLDisplay ptr) noexcept {
    return attach_unique(ptr, eglTerminate);
}

} // end of namespace egl_helper

int main() {
    auto display = attach_unique(wl_display_connect(nullptr));
    auto [compositor, shell, seat] = register_global<wl_compositor, wl_shell, wl_seat>(display.get());
    auto egl_display = attach_unique(eglGetDisplay(display.get()));
    if (!eglInitialize(egl_display.get(), nullptr, nullptr)) {
        std::cerr << "eglInitialize failed..." << std::endl;
        return -1;
    }
    auto surface = attach_unique(wl_compositor_create_surface(compositor.get()));
    auto shell_surface = attach_unique(wl_shell_get_shell_surface(shell.get(), surface.get()));
    static float resolution[2] = { 800, 600 };
    static auto& [cx, cy] = resolution;
    auto egl_window = attach_unique(wl_egl_window_create(surface.get(), cx, cy),
                                    wl_egl_window_destroy);
    {
        static constexpr wl_shell_surface_listener listener {
            .ping = [](void*, wl_shell_surface* shell_surface, uint32_t serial) noexcept {
                wl_shell_surface_pong(shell_surface, serial);
            },
            .configure = [](void* egl_window,
                            wl_shell_surface* shell_surface,
                            uint32_t edges,
                            int32_t width,
                            int32_t height) noexcept
            {
                wl_egl_window_resize(reinterpret_cast<wl_egl_window*>(egl_window),
                                     width,
                                     height,
                                     0, 0);
                glViewport(0, 0, cx = width, cy = height);
            },
            .popup_done = [](auto...) noexcept { },
        };
        if (0 != wl_shell_surface_add_listener(shell_surface.get(), &listener, egl_window.get())) {
            std::cerr << "wl_shell_surface_add_listner failed..." << std::endl;
            return -1;
        }
    }
    wl_shell_surface_set_toplevel(shell_surface.get());
    {
        static constexpr wl_seat_listener listener {
            .capabilities = [](void*, wl_seat* seat_raw, uint32_t caps) noexcept {
                if (caps & WL_SEAT_CAPABILITY_POINTER) {
                    std::cout << "pointer device found." << std::endl;
                }
                if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
                    std::cout << "keyboard device found." << std::endl;
                }
                if (caps & WL_SEAT_CAPABILITY_TOUCH) {
                    std::cout << "touch device found." << std::endl;
                }
                std::cout << "seat capability: " << caps << std::endl;
            },
            .name = [](void*, wl_seat* seat_raw, char const* name) noexcept {
                std::cout << name << std::endl;
            },
        };
        if (0 != wl_seat_add_listener(seat.get(), &listener, nullptr)) {
            std::cerr << "wl_seat_add_listener failed..." << std::endl;
            return -1;
        }
    }
    static int key_input = 0;
    auto keyboard = attach_unique(wl_seat_get_keyboard(seat.get()));
    {
        static constexpr wl_keyboard_listener listener {
            .keymap = [](auto...) noexcept { },
            .enter = [](auto...) noexcept { },
            .leave = [](auto...) noexcept { },
            .key = [](void *data,
                      wl_keyboard* keyboard_raw,
                      uint32_t serial,
                      uint32_t time,
                      uint32_t key,
                      uint32_t state) noexcept
            {
                key_input = key;
            },
            .modifiers = [](auto...) noexcept { },
            .repeat_info = [](auto...) noexcept { },
        };
        if (0 != wl_keyboard_add_listener(keyboard.get(), &listener, nullptr)) {
            std::cerr << "wl_keyboard_add_listener failed..." << std::endl;
            return -1;
        }
    }
    static float pt[2] = { -256, -256 };
    static auto& [px, py] = pt;
    auto pointer = attach_unique(wl_seat_get_pointer(seat.get()));
    {
        static constexpr wl_pointer_listener listener {
            .enter = [](auto...) noexcept { },
            .leave = [](auto...) noexcept { },
            .motion = [](void*, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) noexcept {
                px = wl_fixed_to_int(sx);
                py = cy - wl_fixed_to_int(sy) - 1;
            },
            .button = [](auto...) noexcept { },
            .axis = [](auto...) noexcept { },
            .frame = [](auto...) noexcept { },
        };
        if (0 != wl_pointer_add_listener(pointer.get(), &listener, nullptr)) {
            std::cerr << "wl_pointer_add_listener failed..." << std::endl;
            return -1;
        }
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        std::cerr << "eglBindAPI(EGL_OPENGL_ES_API) failed..." << std::endl;
        return -1;
    }
    EGLint attributes[] = {
        EGL_LEVEL, 0,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE,
    };
    EGLConfig config;
    EGLint num_config;
    if (!eglChooseConfig(egl_display.get(), attributes, &config, 1, &num_config)) {
        std::cerr << "eglChooseConfig failed..." << std::endl;
        return -1;
    }
    EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    auto egl_context = attach_unique(eglCreateContext(egl_display.get(),
                                                      config,
                                                      nullptr,
                                                      contextAttributes),
                                     [&egl_display](auto ptr) {
                                         eglDestroyContext(egl_display.get(),
                                                           ptr);
                                     });
    auto egl_surface = attach_unique(eglCreateWindowSurface(egl_display.get(),
                                                            config,
                                                            egl_window.get(),
                                                            nullptr),
                                     [&egl_display](auto ptr) {
                                         eglDestroySurface(egl_display.get(),
                                                           ptr);
                                     });
    auto eglMadeCurrent = eglMakeCurrent(egl_display.get(),
                                         egl_surface.get(),
                                         egl_surface.get(),
                                         egl_context.get());
    auto vid = glCreateShader(GL_VERTEX_SHADER);
    auto fid = glCreateShader(GL_FRAGMENT_SHADER);
#define CODE(x) (#x)
    auto vcd = CODE(
        attribute vec4 position;
        varying vec2 vert;
        void main(void) {
            vert = position.xy;
            gl_Position = position;
        }
    );
    auto fcd = CODE(
        precision mediump float;
        varying vec2 vert;
        uniform vec2 resolution;
        uniform vec2 pointer;
        void main(void) {
            float brightness = length(gl_FragCoord.xy - resolution / 2.0) / length(resolution);
            brightness = 1.0 - brightness;
            gl_FragColor = vec4(0.0, 0.0, brightness, brightness);
            float radius = length(pointer - gl_FragCoord.xy);
            float touchMark = smoothstep(16.0, 40.0, radius);
            gl_FragColor *= touchMark;
        }
    );
#undef CODE
    auto compile = [](auto id, auto code) {
        glShaderSource(id, 1, &code, nullptr);
        glCompileShader(id);
        GLint result;
        glGetShaderiv(id, GL_COMPILE_STATUS, &result);
        GLint infoLogLength = 0;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infoLogLength);
        if (infoLogLength) {
            std::vector<char> buf(infoLogLength);
            glGetShaderInfoLog(id, infoLogLength, nullptr, &buf.front());
            std::cerr << "<<<" << std::endl;
            std::cerr << code << std::endl;
            std::cerr << "---" << std::endl;
            std::cerr << std::string(buf.begin(), buf.end()).c_str() << std::endl;
            std::cerr << ">>>" << std::endl;
        }
        return result;
    };
    if (!compile(vid, vcd)) {
        std::cerr << "vertex shader compilation failed..." << std::endl;
        return -1;
    }
    if (!compile(fid, fcd)) {
        std::cerr << "fragment shader compilation failed..." << std::endl;
        return -1;
    }
    auto program = glCreateProgram();
    if (!program) {
        std::cerr << "glCreateProgram failed..." << std::endl;
        return -1;
    }
    glAttachShader(program, vid);
    glAttachShader(program, fid);
    glDeleteShader(vid);
    glDeleteShader(fid);
    glBindAttribLocation(program, 0, "position");
    glLinkProgram(program);
    {
        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        assert(linked);
        GLint length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        assert(0 == length);
    }
    glUseProgram(program);
    glFrontFace(GL_CW);
    for (;;) {
        auto ret = wl_display_dispatch_pending(display.get());
        if (-1 == ret) break;
        if (key_input == 1) {
            std::cout << "Bye" << std::endl;
            break;
        }
        glClearColor(0.0, 0.7, 0.0, 0.7);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(program);
        float vertices[] = {
            -1, +1, 0,
            +1, +1, 0,
            +1, -1, 0,
            -1, -1, 0,
        };
        glUniform2fv(glGetUniformLocation(program, "resolution"), 1, resolution);
        glUniform2fv(glGetUniformLocation(program, "pointer"), 1, pt);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        eglSwapBuffers(egl_display.get(), egl_surface.get());
    }
    glDeleteProgram(program);
    return 0;
}
