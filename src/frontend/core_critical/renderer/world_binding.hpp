#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace mw34::world {
// The caller supplies a freshly read source snapshot. Addresses are lookup
// hints only; exact source bytes establish whether a decoded mesh can be reused.
// The caller serializes cache mutation and lookup; this class owns no locks.
template <class T> class SnapshotCache {
    struct Entry {
        std::vector<unsigned char> bytes;
        std::shared_ptr<const T> value;
    };
    std::map<std::uint32_t, Entry> entries_;
    std::size_t maximum_entries_, maximum_bytes_, cached_source_bytes_{};

  public:
    // The byte budget covers copied source snapshots, not decoded T storage.
    // This is a bounded cache with explicit refusal, rather than an evicting LRU.
    SnapshotCache(std::size_t entries, std::size_t bytes)
        : maximum_entries_(entries), maximum_bytes_(bytes) {}
    std::shared_ptr<const T> find(std::uint32_t key,
                                  const std::vector<unsigned char> &bytes) const {
        // A reused guest address is not enough: compare every supplied source
        // byte before returning the immutable decoded value. A mismatch is a miss.
        const auto found = entries_.find(key);
        return found != entries_.end() && found->second.bytes == bytes ? found->second.value
                                                                       : nullptr;
    }
    void put(std::uint32_t key, std::vector<unsigned char> bytes, std::shared_ptr<const T> value) {
        // Replacing a key releases its old source-byte charge before checking
        // the new budget. Returned shared owners may keep an old decoded mesh
        // alive after replacement; the cache does not invalidate their lifetime.
        if (!value)
            throw std::runtime_error("World snapshot cache cannot store an empty value");
        const auto previous = entries_.find(key);
        const std::size_t replaced_source_bytes =
            previous == entries_.end() ? 0 : previous->second.bytes.size();
        if ((previous == entries_.end() && entries_.size() >= maximum_entries_) ||
            bytes.size() > maximum_bytes_ - (cached_source_bytes_ - replaced_source_bytes))
            throw std::runtime_error("World snapshot cache capacity exceeded");
        const auto new_source_bytes = cached_source_bytes_ - replaced_source_bytes + bytes.size();
        entries_.insert_or_assign(key, Entry{std::move(bytes), std::move(value)});
        cached_source_bytes_ = new_source_bytes;
    }
    std::size_t size() const { return entries_.size(); }
    std::size_t bytes() const { return cached_source_bytes_; }
};

inline std::optional<std::size_t> polygon_slot(std::uint32_t base, std::size_t count,
                                               std::uint32_t address) {
    // Original polygon records have a 36-byte stride. Reject earlier, misaligned
    // and one-past addresses using widened arithmetic, without reading memory.
    // The caller remains responsible for the source array's ownership and count.
    if (address < base)
        return {};
    const auto byte_offset = std::uint64_t(address) - base;
    if (byte_offset % 36 || byte_offset / 36 >= count)
        return {};
    return std::size_t(byte_offset / 36);
}
} // namespace mw34::world
