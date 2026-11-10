#include "chrononmotion/preview/AssetServer.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <unordered_set>

namespace chrononmotion::preview {
    namespace {

        std::string normalizedPath(std::string path) {
            if (path.empty()) throw std::invalid_argument("AssetServer: asset path is empty");
            return std::filesystem::path(std::move(path)).lexically_normal().string();
        }

        std::vector<std::byte> readBytes(const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("AssetServer: cannot open " + path.string());
            std::vector<char> raw((std::istreambuf_iterator<char>(input)),
                                  std::istreambuf_iterator<char>());
            std::vector<std::byte> bytes(raw.size());
            if (!raw.empty()) std::memcpy(bytes.data(), raw.data(), raw.size());
            return bytes;
        }

        std::optional<std::pair<std::filesystem::file_time_type, std::uintmax_t>>
        snapshot(const std::string& path) {
            std::error_code error;
            const auto time = std::filesystem::last_write_time(path, error);
            if (error) return std::nullopt;
            const auto size = std::filesystem::file_size(path, error);
            if (error) return std::nullopt;
            return std::make_pair(time, size);
        }

    }// namespace

    AssetServer::AssetServer(TaskPool& ioPool)
        : ioPool_(ioPool) {
        // Index zero is the invalid handle; it is never handed out.
        slots_.resize(1);

        registerLoader<TextAsset>([](AssetLoadContext& context) {
            auto asset = std::make_shared<TextAsset>();
            asset->text.assign(reinterpret_cast<const char*>(context.bytes.data()), context.bytes.size());
            return asset;
        });

        registerLoader<BinaryAsset>([](AssetLoadContext& context) {
            auto asset = std::make_shared<BinaryAsset>();
            asset->bytes = context.bytes;
            return asset;
        });

        registerLoader<MotionSceneAsset>([](AssetLoadContext& context) {
            const std::string_view json(reinterpret_cast<const char*>(context.bytes.data()),
                                        context.bytes.size());
            auto asset = std::make_shared<MotionSceneAsset>();
            asset->scene = std::make_shared<motion::MotionScene>(motion::sceneFromJson(json));
            return asset;
        });

        registerLoader<::chrononmotion::GltfAsset>([](AssetLoadContext& context) {
            const std::filesystem::path source(context.path);
            const std::filesystem::path base = source.parent_path();
            auto resolver = [&context, base](std::string_view uri) -> std::vector<std::byte> {
                const std::filesystem::path dependency = (base / std::string(uri)).lexically_normal();
                context.addDependency<BinaryAsset>(dependency.string());
                return readBytes(dependency);
            };
            const std::string_view json(reinterpret_cast<const char*>(context.bytes.data()),
                                        context.bytes.size());
            auto asset = std::make_shared<::chrononmotion::GltfAsset>(
                    ::chrononmotion::loadGltf(json, resolver));
            return asset;
        });
    }

    AssetServer::~AssetServer() {
        stopWatching();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shuttingDown_ = true;
        }
        cv_.notify_all();
        // In-flight loaders publish into this object. Wait for them before the
        // registry is destroyed; the owning TaskPool must outlive the server.
        ioPool_.waitIdle();
    }

    AssetId AssetServer::createAsset(std::type_index type, std::string path,
                                     AssetLoadOptions options,
                                     std::shared_ptr<LoaderBase> loader) {
        path = normalizedPath(std::move(path));
        std::lock_guard<std::mutex> lock(mutex_);

        if (shuttingDown_)
            throw std::runtime_error("AssetServer: server is shutting down");

        const auto existing = byPath_.find({type, path});
        if (existing != byPath_.end()) {
            Slot& slot = slots_[existing->second];
            if (options.watch) slot.watch = true;
            return slot.key.id;
        }

        std::uint32_t index = 0;
        std::uint32_t generation = 1;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
            generation = slots_[index].key.id.generation() + 1;
            if (generation == 0) generation = 1;
        } else {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
        }

        Slot& slot = slots_[index];
        slot = Slot{};
        slot.key = AssetKey{type, AssetId::make(index, generation)};
        slot.path = path;
        slot.state = AssetState::Loading;
        slot.loader = std::move(loader);
        slot.watch = options.watch;
        byKey_[slot.key] = index;
        byPath_[{type, path}] = index;

        const AssetId id = slot.key.id;
        scheduleLoadLocked(index);
        return id;
    }

    AssetKey AssetServer::loadPath(std::type_index type, std::string path, AssetLoadOptions options) {
        std::shared_ptr<LoaderBase> loader;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = loaders_.find(type);
            if (it == loaders_.end())
                throw std::invalid_argument("AssetServer: no loader registered for dependency type");
            loader = it->second;
        }
        const AssetId id = createAsset(type, std::move(path), options, std::move(loader));
        return AssetKey{type, id};
    }

    void AssetServer::loadPathDependency(AssetKey parent, std::type_index type, std::string path) {
        try {
            const AssetKey child = loadPath(type, std::move(path), AssetLoadOptions{true});
            addDependencyKeys(parent, child);
        } catch (...) {
            // A missing external dependency must fail the parent's load, not
            // terminate the IO worker. The loader sees the exception.
            throw;
        }
    }

    void AssetServer::scheduleLoadLocked(std::uint32_t index) {
        Slot& slot = slots_[index];
        const AssetKey key = slot.key;
        const std::string path = slot.path;
        const auto loader = slot.loader;
        if (!loader) {
            slot.state = AssetState::Failed;
            return;
        }
        auto task = [this, key, path, loader] {
            loadTask(key.id.index(), key, path, loader);
        };
        try {
            (void)ioPool_.submit(std::move(task));
        } catch (const std::exception& error) {
            // The pool may be shutting down. Keep the slot in Failed rather
            // than leaving a Loading state that would never be notified.
            Slot* failed = findSlotLocked(key);
            if (failed) {
                failed->state = AssetState::Failed;
                events_.push_back(AssetEvent{AssetEventKind::Failed, key.type,
                                             key.id.index(), key.id.generation(),
                                             failed->path, error.what()});
            }
            cv_.notify_all();
        }
    }

    void AssetServer::loadTask(std::uint32_t index, AssetKey key, std::string path,
                               std::shared_ptr<LoaderBase> loader) {
        (void)index;
        try {
            const auto before = snapshot(path);
            if (!before)
                throw std::runtime_error("AssetServer: cannot stat " + path);
            const auto bytes = readBytes(path);
            AssetLoadContext context{
                    path,
                    bytes,
                    *this,
                    key,
                    [this, key](std::type_index type, std::string dependencyPath) {
                        loadPathDependency(key, type, std::move(dependencyPath));
                    }};
            auto data = loader->load(context);
            if (!data)
                throw std::runtime_error("AssetServer: loader returned null for " + path);
            finishSuccess(key, std::move(data), before->second, before->first);
        } catch (const std::exception& error) {
            finishFailure(key, error.what());
        } catch (...) {
            finishFailure(key, "AssetServer: unknown loader failure");
        }
    }

    void AssetServer::finishSuccess(AssetKey key, std::shared_ptr<void> data,
                                    std::uintmax_t size,
                                    std::filesystem::file_time_type writeTime) {
        std::vector<AssetKey> dependents;
        bool shouldReloadAgain = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Slot* slot = findSlotLocked(key);
            if (!slot) return;

            const bool modified = slot->hasBeenReady;
            slot->data = std::move(data);
            slot->state = AssetState::Ready;
            slot->hasBeenReady = true;
            slot->size = size;
            slot->writeTime = writeTime;
            slot->evictable = slot->refCount == 0 && slot->evictable;
            events_.push_back(AssetEvent{modified ? AssetEventKind::Modified : AssetEventKind::Added,
                                         key.type, key.id.index(), key.id.generation(),
                                         slot->path, {}});

            if (modified) {
                dependents = slot->dependents;
            }
            if (slot->reloadPending) {
                slot->reloadPending = false;
                shouldReloadAgain = true;
            }
            cv_.notify_all();
        }

        for (const AssetKey& dependent : dependents) {
            // A dependent that is already loading will set reloadPending and
            // pick the change up when that load completes.
            reloadKey(dependent);
        }
        if (shouldReloadAgain) reloadKey(key);
    }

    void AssetServer::finishFailure(AssetKey key, std::string message) {
        std::vector<AssetKey> dependents;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Slot* slot = findSlotLocked(key);
            if (!slot) return;
            slot->state = AssetState::Failed;
            events_.push_back(AssetEvent{AssetEventKind::Failed, key.type,
                                         key.id.index(), key.id.generation(),
                                         slot->path, std::move(message)});
            dependents = slot->dependents;
            cv_.notify_all();
        }
        for (const AssetKey& dependent : dependents) {
            reloadKey(dependent);
        }
    }

    AssetServer::Slot* AssetServer::findSlotLocked(AssetKey key) {
        const auto it = byKey_.find(key);
        if (it == byKey_.end()) return nullptr;
        return &slots_[it->second];
    }

    const AssetServer::Slot* AssetServer::findSlotLocked(AssetKey key) const {
        const auto it = byKey_.find(key);
        if (it == byKey_.end()) return nullptr;
        return &slots_[it->second];
    }

    std::uint32_t AssetServer::indexOfLocked(AssetKey key) const {
        const auto it = byKey_.find(key);
        return it == byKey_.end() ? 0 : it->second;
    }

    AssetState AssetServer::stateOf(AssetKey key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const Slot* slot = findSlotLocked(key);
        return slot ? slot->state : AssetState::Unknown;
    }

    std::shared_ptr<void> AssetServer::dataOf(AssetKey key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const Slot* slot = findSlotLocked(key);
        if (!slot || (slot->state != AssetState::Ready && slot->state != AssetState::Loading)) {
            return nullptr;
        }
        return slot->data;
    }

    bool AssetServer::waitKey(AssetKey key) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this, key] {
            const Slot* slot = findSlotLocked(key);
            return slot == nullptr || slot->state != AssetState::Loading;
        });
        const Slot* slot = findSlotLocked(key);
        return slot != nullptr &&
               (slot->state == AssetState::Ready || slot->state == AssetState::Failed);
    }

    void AssetServer::reloadKey(AssetKey key) {
        std::lock_guard<std::mutex> lock(mutex_);
        Slot* slot = findSlotLocked(key);
        if (!slot) return;
        if (slot->state == AssetState::Loading) {
            slot->reloadPending = true;
            return;
        }
        slot->state = AssetState::Loading;
        scheduleLoadLocked(static_cast<std::uint32_t>(key.id.index()));
    }

    void AssetServer::unloadKey(AssetKey key) {
        std::vector<AssetKey> dependents;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const std::uint32_t index = indexOfLocked(key);
            if (index == 0) return;
            Slot& slot = slots_[index];
            dependents = slot.dependents;

            events_.push_back(AssetEvent{AssetEventKind::Removed, key.type,
                                         key.id.index(), key.id.generation(),
                                         slot.path, {}});
            removeKeyFromGraphLocked(key, dependents);
            byPath_.erase({key.type, slot.path});
            byKey_.erase(key);

            slot = Slot{};
            slot.key = AssetKey{key.type, AssetId::make(index, key.id.generation() + 1)};
            if (slot.key.id.generation() == 0)
                slot.key = AssetKey{key.type, AssetId::make(index, 1)};
            slot.state = AssetState::Unloaded;
            free_.push_back(index);
            cv_.notify_all();
        }
        for (const AssetKey& dependent : dependents) reloadKey(dependent);
    }

    void AssetServer::removeKeyFromGraphLocked(AssetKey key, std::vector<AssetKey>& dependents) {
        for (Slot& slot : slots_) {
            if (!slot.key.valid()) continue;
            slot.dependencies.erase(
                    std::remove(slot.dependencies.begin(), slot.dependencies.end(), key),
                    slot.dependencies.end());
            slot.dependents.erase(
                    std::remove(slot.dependents.begin(), slot.dependents.end(), key),
                    slot.dependents.end());
        }
        dependents.erase(std::remove(dependents.begin(), dependents.end(), key), dependents.end());
    }

    void AssetServer::addDependencyKeys(AssetKey dependent, AssetKey dependency) {
        if (!dependent.valid() || !dependency.valid())
            throw std::invalid_argument("AssetServer::addDependency: invalid handle");
        if (dependent == dependency)
            throw std::invalid_argument("AssetServer::addDependency: self dependency");
        std::lock_guard<std::mutex> lock(mutex_);
        Slot* parent = findSlotLocked(dependent);
        Slot* child = findSlotLocked(dependency);
        if (!parent || !child)
            throw std::invalid_argument("AssetServer::addDependency: asset is not loaded");

        // Adding parent -> child is a cycle iff parent is already reachable
        // from child through child's dependencies.
        std::unordered_set<AssetKey, AssetKeyHash> seen;
        std::vector<AssetKey> stack{child->key};
        while (!stack.empty()) {
            const AssetKey current = stack.back();
            stack.pop_back();
            if (!seen.insert(current).second) continue;
            if (current == dependent)
                throw std::invalid_argument("AssetServer::addDependency: dependency cycle");
            const Slot* slot = findSlotLocked(current);
            if (!slot) continue;
            for (const AssetKey& next : slot->dependencies) stack.push_back(next);
        }

        if (std::find(parent->dependencies.begin(), parent->dependencies.end(), dependency) !=
            parent->dependencies.end())
            return;
        parent->dependencies.push_back(dependency);
        child->dependents.push_back(dependent);
    }

    std::shared_ptr<void> AssetServer::acquireKey(AssetKey key) {
        std::lock_guard<std::mutex> lock(mutex_);
        Slot* slot = findSlotLocked(key);
        if (!slot || slot->state != AssetState::Ready || !slot->data) return nullptr;
        ++slot->refCount;
        slot->evictable = false;
        return slot->data;
    }

    void AssetServer::release(AssetKey key) {
        std::lock_guard<std::mutex> lock(mutex_);
        Slot* slot = findSlotLocked(key);
        if (!slot) return;
        if (slot->refCount > 0) --slot->refCount;
        if (slot->refCount == 0) slot->evictable = true;
        cv_.notify_all();
    }

    void AssetServer::collectGarbage() {
        std::vector<AssetKey> evict;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const Slot& slot : slots_) {
                if (!slot.key.valid()) continue;
                if ((slot.state == AssetState::Ready || slot.state == AssetState::Failed) &&
                    slot.refCount == 0 && slot.evictable) {
                    evict.push_back(slot.key);
                }
            }
        }
        for (const AssetKey& key : evict) unloadKey(key);
    }

    std::optional<AssetEvent> AssetServer::pollEvent() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (events_.empty()) return std::nullopt;
        AssetEvent event = std::move(events_.front());
        events_.pop_front();
        return event;
    }

    std::vector<AssetEvent> AssetServer::pollEvents() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<AssetEvent> result;
        result.reserve(events_.size());
        while (!events_.empty()) {
            result.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        return result;
    }

    std::size_t AssetServer::count(AssetState state) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t result = 0;
        for (const Slot& slot : slots_) {
            if (slot.key.valid() && slot.state == state) ++result;
        }
        return result;
    }

    std::uint64_t AssetServer::totalBytes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::uint64_t total = 0;
        for (const Slot& slot : slots_) {
            if (slot.key.valid() && slot.state == AssetState::Ready) {
                total += static_cast<std::uint64_t>(slot.size);
            }
        }
        return total;
    }

    void AssetServer::startWatching(std::chrono::milliseconds interval) {
        if (interval.count() <= 0)
            throw std::invalid_argument("AssetServer::startWatching: interval must be positive");
        if (watcherRunning_.load()) return;
        watchInterval_ = interval;
        watchStop_.store(false);
        watcherRunning_.store(true);
        watcher_ = std::thread([this] {
            while (!watchStop_.load()) {
                pollOnce();
                std::this_thread::sleep_for(watchInterval_);
            }
            watcherRunning_.store(false);
        });
    }

    void AssetServer::stopWatching() {
        watchStop_.store(true);
        if (watcher_.joinable()) watcher_.join();
        watcherRunning_.store(false);
    }

    void AssetServer::pollOnce() {
        struct Candidate {
            AssetKey key;
            std::string path;
            std::filesystem::file_time_type writeTime;
            std::uintmax_t size;
            AssetState state;
        };
        std::vector<Candidate> candidates;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const Slot& slot : slots_) {
                if (!slot.key.valid() || !slot.watch || slot.path.empty()) continue;
                if (slot.state != AssetState::Ready && slot.state != AssetState::Failed) continue;
                candidates.push_back(Candidate{slot.key, slot.path, slot.writeTime,
                                               slot.size, slot.state});
            }
        }

        for (const Candidate& candidate : candidates) {
            const auto current = snapshot(candidate.path);
            if (!current) {
                unloadKey(candidate.key);
                continue;
            }
            if (current->first != candidate.writeTime || current->second != candidate.size) {
                reloadKey(candidate.key);
            }
        }
    }

}// namespace chrononmotion::preview
