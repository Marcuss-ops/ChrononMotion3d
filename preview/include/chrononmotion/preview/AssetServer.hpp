// ChrononMotion preview — the asset server.
//
// Bevy's asset system, reduced to the parts an authoring preview actually
// needs: generational handles, async IO, Added/Modified/Removed/Failed events,
// a dependency graph, polling-based hot reload and reference-counted leases.
// It lives outside the deterministic core and never gives the core a filesystem.

#ifndef CHRONONMOTION_PREVIEW_ASSETSERVER_HPP
#define CHRONONMOTION_PREVIEW_ASSETSERVER_HPP

#include "chrononmotion/preview/TaskPool.hpp"

#include "chrononmotion/assets/GltfReader.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/SceneImport.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chrononmotion::preview {

    struct TextAsset {
        std::string text;
    };

    struct BinaryAsset {
        std::vector<std::byte> bytes;
    };

    struct MotionSceneAsset {
        std::shared_ptr<motion::MotionScene> scene;
    };

    struct AssetId {
        std::uint64_t value{0};

        [[nodiscard]] static AssetId make(std::uint32_t index, std::uint32_t generation) {
            return AssetId{(static_cast<std::uint64_t>(generation) << 32) | index};
        }
        [[nodiscard]] std::uint32_t index() const { return static_cast<std::uint32_t>(value & 0xffffffffu); }
        [[nodiscard]] std::uint32_t generation() const { return static_cast<std::uint32_t>(value >> 32); }
        [[nodiscard]] bool valid() const { return value != 0; }
        bool operator==(const AssetId&) const = default;
    };

    struct AssetKey {
        std::type_index type{typeid(void)};
        AssetId id{};

        [[nodiscard]] bool valid() const { return id.valid() && type != std::type_index(typeid(void)); }
        bool operator==(const AssetKey&) const = default;
    };

    struct AssetKeyHash {
        std::size_t operator()(const AssetKey& key) const noexcept {
            const std::size_t a = std::hash<std::type_index>{}(key.type);
            const std::size_t b = std::hash<std::uint64_t>{}(key.id.value);
            return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
        }
    };

    template<class T>
    struct AssetHandle {
        std::uint32_t index{0};
        std::uint32_t generation{0};

        [[nodiscard]] bool valid() const { return index != 0; }
        [[nodiscard]] AssetId id() const { return AssetId::make(index, generation); }
        bool operator==(const AssetHandle&) const = default;
    };

    enum class AssetState {
        Unknown,
        Loading,
        Ready,
        Failed,
        Unloaded
    };

    enum class AssetEventKind {
        Added,
        Modified,
        Removed,
        Failed
    };

    [[nodiscard]] inline std::string_view assetEventKindName(AssetEventKind kind) {
        switch (kind) {
            case AssetEventKind::Added: return "added";
            case AssetEventKind::Modified: return "modified";
            case AssetEventKind::Removed: return "removed";
            case AssetEventKind::Failed: return "failed";
        }
        return "unknown";
    }

    struct AssetEvent {
        AssetEventKind kind{AssetEventKind::Added};
        std::type_index type{typeid(void)};
        std::uint32_t index{0};
        std::uint32_t generation{0};
        std::string path{};
        std::string message{};

        [[nodiscard]] bool valid() const { return index != 0 && type != std::type_index(typeid(void)); }
        [[nodiscard]] AssetKey key() const { return AssetKey{type, AssetId::make(index, generation)}; }

        template<class T>
        [[nodiscard]] AssetHandle<T> handle() const {
            if (type != std::type_index(typeid(T)))
                throw std::invalid_argument("AssetEvent::handle: asset type mismatch");
            return AssetHandle<T>{index, generation};
        }
    };

    struct AssetLoadOptions {
        /// Poll this file for changes (hot reload). Defaults to on.
        bool watch{true};
    };

    struct AssetLoadContext {
        std::string path;
        const std::vector<std::byte>& bytes;
        class AssetServer& server;
        AssetKey key;
        std::function<void(std::type_index, std::string)> addDependencyPath;

        template<class T>
        void addDependency(std::string dependencyPath) {
            if (!addDependencyPath) return;
            addDependencyPath(std::type_index(typeid(T)), std::move(dependencyPath));
        }
    };

    class AssetServer;

    template<class T>
    class AssetLease {
    public:
        AssetLease() = default;

        AssetLease(AssetLease&& other) noexcept
            : server_(other.server_), key_(other.key_), data_(std::move(other.data_)) {
            other.server_ = nullptr;
            other.key_ = AssetKey{};
        }

        AssetLease& operator=(AssetLease&& other) noexcept {
            if (this != &other) {
                reset();
                server_ = other.server_;
                key_ = other.key_;
                data_ = std::move(other.data_);
                other.server_ = nullptr;
                other.key_ = AssetKey{};
            }
            return *this;
        }

        ~AssetLease();

        AssetLease(const AssetLease&) = delete;
        AssetLease& operator=(const AssetLease&) = delete;

        [[nodiscard]] bool valid() const { return server_ != nullptr && key_.valid() && data_ != nullptr; }
        explicit operator bool() const { return valid(); }
        [[nodiscard]] std::shared_ptr<T> get() const { return data_; }
        [[nodiscard]] std::shared_ptr<T> operator->() const { return data_; }
        [[nodiscard]] AssetHandle<T> handle() const {
            return AssetHandle<T>{key_.id.index(), key_.id.generation()};
        }

        void reset();

    private:
        friend class AssetServer;
        AssetLease(AssetServer* server, AssetKey key, std::shared_ptr<T> data)
            : server_(server), key_(key), data_(std::move(data)) {}

        AssetServer* server_{nullptr};
        AssetKey key_{};
        std::shared_ptr<T> data_{};
    };

    class AssetServer {
    public:
        explicit AssetServer(TaskPool& ioPool);
        ~AssetServer();

        AssetServer(const AssetServer&) = delete;
        AssetServer& operator=(const AssetServer&) = delete;
        AssetServer(AssetServer&&) = delete;
        AssetServer& operator=(AssetServer&&) = delete;

        template<class T>
        void registerLoader(std::function<std::shared_ptr<T>(AssetLoadContext&)> loader) {
            if (!loader) throw std::invalid_argument("AssetServer::registerLoader: loader is empty");
            auto typed = std::make_shared<TypedLoader<T>>(std::move(loader));
            std::lock_guard<std::mutex> lock(mutex_);
            loaders_[std::type_index(typeid(T))] = std::move(typed);
        }

        template<class T>
        [[nodiscard]] AssetHandle<T> load(std::string path, AssetLoadOptions options = {}) {
            std::shared_ptr<LoaderBase> loader;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto it = loaders_.find(std::type_index(typeid(T)));
                if (it == loaders_.end())
                    throw std::invalid_argument("AssetServer::load: no loader registered for this type");
                loader = it->second;
            }
            const AssetId id = createAsset(std::type_index(typeid(T)), std::move(path), options, std::move(loader));
            return AssetHandle<T>{id.index(), id.generation()};
        }

        template<class T>
        [[nodiscard]] AssetState state(AssetHandle<T> handle) const {
            return stateOf(keyOf(handle));
        }

        template<class T>
        [[nodiscard]] std::shared_ptr<T> get(AssetHandle<T> handle) const {
            auto data = dataOf(keyOf(handle));
            return data ? std::static_pointer_cast<T>(data) : nullptr;
        }

        /// Wait until an asset is no longer Loading. Returns true when the
        /// asset exists and is Ready or Failed. Returns false for an invalid
        /// or unloaded handle.
        template<class T>
        bool wait(AssetHandle<T> handle) {
            return waitKey(keyOf(handle));
        }

        template<class T>
        [[nodiscard]] AssetLease<T> acquire(AssetHandle<T> handle) {
            const AssetKey key = keyOf(handle);
            auto data = acquireKey(key);
            if (!data) return {};
            return AssetLease<T>(this, key, std::static_pointer_cast<T>(data));
        }

        template<class T, class U>
        void addDependency(AssetHandle<T> dependent, AssetHandle<U> dependency) {
            addDependencyKeys(keyOf(dependent), keyOf(dependency));
        }

        template<class T>
        void reload(AssetHandle<T> handle) {
            reloadKey(keyOf(handle));
        }

        template<class T>
        void unload(AssetHandle<T> handle) {
            unloadKey(keyOf(handle));
        }

        void addDependencyKeys(AssetKey dependent, AssetKey dependency);

        /// Evict Ready/Failed assets that have been leased to zero (or were
        /// explicitly released) and are therefore evictable.
        void collectGarbage();

        [[nodiscard]] std::optional<AssetEvent> pollEvent();
        [[nodiscard]] std::vector<AssetEvent> pollEvents();

        [[nodiscard]] std::size_t count(AssetState state) const;
        [[nodiscard]] std::uint64_t totalBytes() const;

        void startWatching(std::chrono::milliseconds interval = std::chrono::milliseconds(250));
        void stopWatching();
        [[nodiscard]] bool watching() const { return watcherRunning_.load(); }

        /// One deterministic watch pass. Tests call this directly instead of
        /// sleeping for the watcher thread. Production uses startWatching().
        void pollOnce();

        template<class T>
        [[nodiscard]] AssetKey keyOf(AssetHandle<T> handle) const {
            return AssetKey{std::type_index(typeid(T)), handle.id()};
        }

        /// Internal, but public for AssetLease's destructor.
        void release(AssetKey key);

    private:
        class LoaderBase {
        public:
            virtual ~LoaderBase() = default;
            virtual std::shared_ptr<void> load(AssetLoadContext& context) = 0;
        };

        template<class T>
        class TypedLoader final : public LoaderBase {
        public:
            explicit TypedLoader(std::function<std::shared_ptr<T>(AssetLoadContext&)> loader)
                : loader_(std::move(loader)) {}

            std::shared_ptr<void> load(AssetLoadContext& context) override {
                return std::static_pointer_cast<void>(loader_(context));
            }

        private:
            std::function<std::shared_ptr<T>(AssetLoadContext&)> loader_;
        };

        struct Slot {
            AssetKey key{};
            std::string path{};
            AssetState state{AssetState::Unknown};
            std::shared_ptr<void> data{};
            std::shared_ptr<LoaderBase> loader{};
            std::uint32_t refCount{0};
            bool evictable{false};
            bool hasBeenReady{false};
            bool reloadPending{false};
            bool watch{true};
            std::filesystem::file_time_type writeTime{};
            std::uintmax_t size{0};
            std::vector<AssetKey> dependencies{};
            std::vector<AssetKey> dependents{};
        };

        AssetId createAsset(std::type_index type, std::string path, AssetLoadOptions options,
                            std::shared_ptr<LoaderBase> loader);
        AssetKey loadPath(std::type_index type, std::string path, AssetLoadOptions options);
        void loadPathDependency(AssetKey parent, std::type_index type, std::string path);

        [[nodiscard]] AssetState stateOf(AssetKey key) const;
        [[nodiscard]] std::shared_ptr<void> dataOf(AssetKey key) const;
        bool waitKey(AssetKey key);
        std::shared_ptr<void> acquireKey(AssetKey key);
        void reloadKey(AssetKey key);
        void unloadKey(AssetKey key);

        void scheduleLoadLocked(std::uint32_t index);
        void loadTask(std::uint32_t index, AssetKey key, std::string path,
                      std::shared_ptr<LoaderBase> loader);
        void finishSuccess(AssetKey key, std::shared_ptr<void> data, std::uintmax_t size,
                           std::filesystem::file_time_type writeTime);
        void finishFailure(AssetKey key, std::string message);

        [[nodiscard]] Slot* findSlotLocked(AssetKey key);
        [[nodiscard]] const Slot* findSlotLocked(AssetKey key) const;
        [[nodiscard]] std::uint32_t indexOfLocked(AssetKey key) const;
        void removeKeyFromGraphLocked(AssetKey key, std::vector<AssetKey>& dependents);

        TaskPool& ioPool_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::vector<Slot> slots_{};
        std::vector<std::uint32_t> free_{};
        std::unordered_map<AssetKey, std::uint32_t, AssetKeyHash> byKey_{};
        std::map<std::pair<std::type_index, std::string>, std::uint32_t> byPath_{};
        std::unordered_map<std::type_index, std::shared_ptr<LoaderBase>> loaders_{};
        std::deque<AssetEvent> events_{};
        std::thread watcher_{};
        std::atomic<bool> watcherRunning_{false};
        std::atomic<bool> watchStop_{false};
        std::chrono::milliseconds watchInterval_{250};
        bool shuttingDown_{false};
    };

    template<class T>
    AssetLease<T>::~AssetLease() {
        reset();
    }

    template<class T>
    void AssetLease<T>::reset() {
        if (server_ != nullptr && key_.valid()) {
            server_->release(key_);
        }
        server_ = nullptr;
        key_ = AssetKey{};
        data_.reset();
    }

}// namespace chrononmotion::preview

#endif//CHRONONMOTION_PREVIEW_ASSETSERVER_HPP
