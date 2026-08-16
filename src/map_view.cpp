#include "map_view.hpp"

#include <imgui.h>

#define GLFW_INCLUDE_ES3
#include <GLFW/glfw3.h>

#include <mbgl/gfx/backend_scope.hpp>
#include <mbgl/gfx/context.hpp>
#include <mbgl/gfx/renderable.hpp>
#include <mbgl/gfx/rendering_stats.hpp>
#include <mbgl/gl/renderable_resource.hpp>
#include <mbgl/gl/renderer_backend.hpp>
#include <mbgl/map/map.hpp>
#include <mbgl/renderer/renderer.hpp>
#include <mbgl/renderer/renderer_frontend.hpp>
#include <mbgl/storage/resource_options.hpp>
#include <mbgl/style/style.hpp>
#include <mbgl/util/client_options.hpp>
#include <mbgl/util/run_loop.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

constexpr const char* DefaultStyleUrl = "https://tiles.openfreemap.org/styles/liberty";

class MapRenderBackend;

class MapRenderableResource final : public mbgl::gl::RenderableResource {
public:
    explicit MapRenderableResource(MapRenderBackend& backend_)
        : backend(backend_) {}

    void bind() override;

private:
    MapRenderBackend& backend;
};

class MapRenderBackend final : public mbgl::gl::RendererBackend, public mbgl::gfx::Renderable {
public:
    MapRenderBackend(GLFWwindow* mainWindow_, mbgl::Size initialSize)
        : mbgl::gl::RendererBackend(mbgl::gfx::ContextMode::Unique),
          mbgl::gfx::Renderable(initialSize, std::make_unique<MapRenderableResource>(*this)) {
        mainWindow = mainWindow_;

        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        mapWindow = glfwCreateWindow(1, 1, "SectorOne MapLibre renderer", nullptr, mainWindow);
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

        if (!mapWindow) {
            throw std::runtime_error("Failed to create the MapLibre OpenGL context");
        }

        resize(initialSize);
    }

    ~MapRenderBackend() override {
        GLFWwindow* previousContext = glfwGetCurrentContext();
        glfwMakeContextCurrent(mapWindow);
        if (depthStencil != 0) glDeleteRenderbuffers(1, &depthStencil);
        if (framebuffer != 0) glDeleteFramebuffers(1, &framebuffer);
        if (texture != 0) glDeleteTextures(1, &texture);
        glfwMakeContextCurrent(previousContext == mapWindow ? mainWindow : previousContext);
        glfwDestroyWindow(mapWindow);
    }

    mbgl::gfx::Renderable& getDefaultRenderable() override { return *this; }

    void resize(mbgl::Size newSize) {
        if (newSize.width == 0 || newSize.height == 0 || (newSize == size && texture != 0)) return;

        GLFWwindow* previousContext = glfwGetCurrentContext();
        glfwMakeContextCurrent(mapWindow);
        size = newSize;
        if (texture == 0) glGenTextures(1, &texture);
        if (framebuffer == 0) glGenFramebuffers(1, &framebuffer);
        if (depthStencil == 0) glGenRenderbuffers(1, &depthStencil);

        GLint previousFramebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);

        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     static_cast<GLsizei>(size.width),
                     static_cast<GLsizei>(size.height),
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     nullptr);

        glBindRenderbuffer(GL_RENDERBUFFER, depthStencil);
        glRenderbufferStorage(GL_RENDERBUFFER,
                              GL_DEPTH24_STENCIL8,
                              static_cast<GLsizei>(size.width),
                              static_cast<GLsizei>(size.height));

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthStencil);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
        glfwMakeContextCurrent(previousContext);
    }

    GLuint getTexture() const { return texture; }

    void bindTarget() {
        setFramebufferBinding(framebuffer);
        setViewport(0, 0, size);
    }

protected:
    void activate() override { glfwMakeContextCurrent(mapWindow); }
    void deactivate() override {
        glFlush();
        glfwMakeContextCurrent(mainWindow);
    }

    mbgl::gl::ProcAddress getExtensionFunctionPointer(const char* name) override {
        return reinterpret_cast<mbgl::gl::ProcAddress>(glfwGetProcAddress(name));
    }

    void updateAssumedState() override {
        GLint currentFramebuffer = 0;
        GLint viewport[4]{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFramebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        assumeFramebufferBinding(static_cast<mbgl::gl::FramebufferID>(currentFramebuffer));
        assumeViewport(viewport[0],
                       viewport[1],
                       {static_cast<uint32_t>(viewport[2]), static_cast<uint32_t>(viewport[3])});
        assumeScissorTest(glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
    }

private:
    GLFWwindow* mainWindow = nullptr;
    GLFWwindow* mapWindow = nullptr;
    GLuint texture = 0;
    GLuint framebuffer = 0;
    GLuint depthStencil = 0;
};

void MapRenderableResource::bind() {
    backend.bindTarget();
}

class MapRendererFrontend final : public mbgl::RendererFrontend {
public:
    explicit MapRendererFrontend(MapRenderBackend& backend_)
        : backend(backend_), renderer(std::make_unique<mbgl::Renderer>(backend, 1.0f)) {}

    ~MapRendererFrontend() override = default;

    void reset() override { renderer.reset(); }

    void setObserver(mbgl::RendererObserver& observer) override {
        renderer->setObserver(&observer);
    }

    void update(std::shared_ptr<mbgl::UpdateParameters> parameters) override {
        updateParameters = std::move(parameters);
        dirty = true;
    }

    const mbgl::TaggedScheduler& getThreadPool() const override {
        return backend.getThreadPool();
    }

    bool renderIfNeeded() {
        if (!dirty || !updateParameters || !renderer) return false;
        dirty = false;

        const auto start = std::chrono::steady_clock::now();
        mbgl::gfx::BackendScope scope{backend};
        const auto parameters = updateParameters;
        renderer->render(parameters);
        renderTimeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        renderingStats = backend.getContext().renderingStats();
        return true;
    }

    double getRenderTimeMs() const { return renderTimeMs; }
    const mbgl::gfx::RenderingStats& getRenderingStats() const { return renderingStats; }

private:
    MapRenderBackend& backend;
    std::unique_ptr<mbgl::Renderer> renderer;
    std::shared_ptr<mbgl::UpdateParameters> updateParameters;
    bool dirty = true;
    double renderTimeMs = 0.0;
    mbgl::gfx::RenderingStats renderingStats;
};

} // namespace

class MapView::Impl final : public mbgl::MapObserver {
public:
    Impl()
        : backend(glfwGetCurrentContext(), {InitialWidth, InitialHeight}),
          frontend(backend),
          map(frontend,
              *this,
              mbgl::MapOptions().withSize({InitialWidth, InitialHeight}).withPixelRatio(1.0f),
              mbgl::ResourceOptions().withCachePath("/tmp/sectorone-map-cache.db"),
              mbgl::ClientOptions().withName("SectorOne").withVersion("1.0")) {
        map.jumpTo(mbgl::CameraOptions().withCenter(mbgl::LatLng{31.2304, 121.4737}).withZoom(9.0));
        map.getStyle().loadURL(DefaultStyleUrl);
    }

    void draw() {
        runLoop.runOnce();

        ImGui::SetNextWindowSize(ImVec2(1024.0f, 640.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Map");

        updateMapFps(false);
        drawPerformanceData();

        if (!error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", error.c_str());
        } else if (!styleLoaded) {
            ImGui::TextUnformatted("Loading map style and tiles...");
        }

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const uint32_t width = std::max(1, static_cast<int>(available.x));
        const uint32_t height = std::max(1, static_cast<int>(available.y));
        const mbgl::Size requestedSize{width, height};

        if (requestedSize != backend.getSize()) {
            backend.resize(requestedSize);
            map.setSize(requestedSize);
        }

        if (frontend.renderIfNeeded()) {
            ++renderedFramesInWindow;
            ++totalRenderedFrames;
            updateMapFps(true);
        }
        // MapLibre renders into our off-screen FBO. Restore GLFW's default
        // framebuffer before Dear ImGui renders the application window.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(backend.getTexture())),
                     ImVec2(static_cast<float>(width), static_cast<float>(height)),
                     ImVec2(0.0f, 1.0f),
                     ImVec2(1.0f, 0.0f));

        // Image is display-only and does not capture mouse drags. Overlay an
        // invisible interactive item so dragging the map cannot move the
        // surrounding ImGui window.
        ImGui::SetCursorScreenPos(imageOrigin);
        ImGui::InvisibleButton("##map_input",
                               ImVec2(static_cast<float>(width), static_cast<float>(height)),
                               ImGuiButtonFlags_MouseButtonLeft);
        handleInput(imageOrigin, {static_cast<float>(width), static_cast<float>(height)});
        ImGui::End();
    }

    void onDidFinishLoadingStyle() override { styleLoaded = true; }

    void onDidFailLoadingMap(mbgl::MapLoadError, const std::string& message) override {
        error = message;
    }

    void onDidFinishRenderingFrame(const mbgl::MapObserver::RenderFrameStatus status) override {
        encodingTimeMs = status.frameEncodingTime * 1000.0;
        renderingTimeMs = status.frameRenderingTime * 1000.0;
    }

private:
    void updateMapFps(bool rendered) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - fpsWindowStart).count();
        if (elapsed >= 0.5) {
            mapFps = static_cast<double>(renderedFramesInWindow) / elapsed;
            renderedFramesInWindow = 0;
            fpsWindowStart = now;
        } else if (!rendered && elapsed >= 0.25 && renderedFramesInWindow == 0) {
            mapFps = 0.0;
        }
    }

    void drawPerformanceData() const {
        const auto& stats = frontend.getRenderingStats();
        const auto size = backend.getSize();
        constexpr double BytesPerMiB = 1024.0 * 1024.0;

        ImGui::Text("App: %.1f FPS (%.2f ms) | Map redraw: %.1f FPS",
                    ImGui::GetIO().Framerate,
                    1000.0f / std::max(ImGui::GetIO().Framerate, 0.001f),
                    mapFps);
        ImGui::Text("Map CPU: %.2f ms | Encode: %.2f ms | Render: %.2f ms",
                    frontend.getRenderTimeMs(),
                    encodingTimeMs,
                    renderingTimeMs);
        ImGui::Text("Draw calls: %d | Frames: %llu | Resolution: %u x %u",
                    stats.numDrawCalls,
                    static_cast<unsigned long long>(totalRenderedFrames),
                    size.width,
                    size.height);
        ImGui::Text("Textures: %d (%.1f MiB) | Buffers: %d (%.1f MiB)",
                    stats.numActiveTextures,
                    static_cast<double>(stats.memTextures) / BytesPerMiB,
                    stats.numBuffers,
                    static_cast<double>(stats.memBuffers) / BytesPerMiB);
        ImGui::Separator();
    }

    void handleInput(const ImVec2 origin, const ImVec2 imageSize) {
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (!hovered && !active) return;

        ImGuiIO& io = ImGui::GetIO();
        const mbgl::ScreenCoordinate anchor{
            std::clamp(static_cast<double>(io.MousePos.x - origin.x), 0.0, static_cast<double>(imageSize.x)),
            std::clamp(static_cast<double>(io.MousePos.y - origin.y), 0.0, static_cast<double>(imageSize.y))};

        if (hovered && io.MouseWheel != 0.0f) {
            map.scaleBy(std::pow(2.0, static_cast<double>(io.MouseWheel) * 0.25), anchor);
        }

        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = io.MouseDelta;
            if (delta.x != 0.0f || delta.y != 0.0f) {
                map.moveBy({static_cast<double>(delta.x), static_cast<double>(delta.y)});
            }
        }
    }

    static constexpr uint32_t InitialWidth = 1024;
    static constexpr uint32_t InitialHeight = 640;

    mbgl::util::RunLoop runLoop;
    MapRenderBackend backend;
    MapRendererFrontend frontend;
    mbgl::Map map;
    bool styleLoaded = false;
    std::string error;
    std::chrono::steady_clock::time_point fpsWindowStart = std::chrono::steady_clock::now();
    uint64_t renderedFramesInWindow = 0;
    uint64_t totalRenderedFrames = 0;
    double mapFps = 0.0;
    double encodingTimeMs = 0.0;
    double renderingTimeMs = 0.0;
};

MapView::MapView()
    : impl(std::make_unique<Impl>()) {}

MapView::~MapView() = default;

void MapView::draw() {
    impl->draw();
}
