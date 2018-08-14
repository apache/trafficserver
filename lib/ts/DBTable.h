#pragma once
#include "stdint.h"
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <string_view>

/** What is DBTable?
 * A concurrent hash table.
 * 1. uses bin hashing to allow concurrent operations on seperate locked maps. @see PartitionedMap
 * 2. uses std::shared_ptr<element> to provide durable references. @see DBTable
 *
 * WARNING: this table is thread safe, but the elements are not protected from concurrency by these locks.
 * Locks only protect access operations from concurrent insert and delete, not from concurrent value modification.
 * So you should only store atomic-like elements in it.
 */

////////////////////////////////////////////////////////////////////////////////////////
// PartitionedMap
//
/// Intended to provide a thread safe lookup.
/** A key is hashed into a bin.
 * Each bin has independent map of Ket,Value pairs.
 * Each bin has shared_mutex to allow multilple readers or one writer to the table.
 * The lock only protects against rehashing that part of map.
 * It does not protect the values in the map, they are expected to be atomic, or protected through other methods. @see Extendible
 */
template <typename Key_t, typename Value_t, typename Hasher_t = std::hash<Key_t>, typename Mutex_t = std::shared_mutex>
struct PartitionedMap {
  using KeyType      = Key_t;
  using ValueType    = Value_t;
  using HasherType   = Hasher_t;
  using Map_t        = std::unordered_map<Key_t, Value_t, Hasher_t>;
  using AccessLock_t = std::shared_lock<Mutex_t>; // shared access when reading data
  using ResizeLock_t = std::unique_lock<Mutex_t>; // exclusive access when adding or removing data

public:
  PartitionedMap(size_t num_partitions) : part_maps(num_partitions), part_access(num_partitions)
  {
    for (auto map : part_maps) {
      map.max_load_factor(16);
    }
  }

  Map_t &
  lockPartMap(const Key_t &key, ResizeLock_t &lock)
  {
    size_t hash   = HasherType()(key);
    auto part_idx = hash % part_access.size();
    lock          = ResizeLock_t(part_access[part_idx]);

    return part_maps[part_idx];
  }

  Map_t const &
  getPartMap(const Key_t &key, AccessLock_t &lock)
  {
    size_t hash   = HasherType()(key);
    auto part_idx = hash % part_access.size();
    lock          = AccessLock_t(part_access[part_idx]);

    return part_maps[part_idx];
  }

  /// return true if value retrieved.
  bool
  find(const Key_t &key, Value_t &val)
  {
    // the map could rehash during a concurrent put, and mess with the elm
    AccessLock_t lck;
    Map_t const &map = getPartMap(key, lck);
    auto elm         = map.find(key);
    if (elm != map.end()) {
      val = elm->second;
      return true;
    }

    return false;
  }

  // lock access and read value
  Value_t operator[](const Key_t &key) const
  {
    Value_t val = {};
    find(key, val);
    return val;
  }

  // lock access and reference value
  Value_t &
  obtain(const Key_t &key)
  {
    ResizeLock_t lck;
    return lockPartMap(key, lck)[key];
  }

  // lock access and reference value
  Value_t &operator[](const Key_t &key) { return obtain(key); }

  void
  put(const Key_t &key, Value_t &val)
  {
    ResizeLock_t lck;
    lockPartMap(key, lck)[key] = val;
  }

  Value_t
  pop(const Key_t &key)
  {
    ResizeLock_t lck;
    Map_t &map = lockPartMap(key, lck);

    Value_t val = map[key];
    map.erase(key);
    return val;
  }

  void
  clear()
  {
    for (int part_idx = 0; part_idx < part_access.size(); ++part_idx) {
      ResizeLock_t lck(part_access[part_idx]);
      part_maps[part_idx].clear();
    }
  }

  /**
   * @brief used inplace of an iterator.
   * @param callback - processes and element. Return true if we can abort iteration.
   */
  void
  visit(std::function<bool(Key_t const &, Value_t &)> callback)
  {
    for (int part_idx = 0; part_idx < part_access.size(); ++part_idx) {
      AccessLock_t lck(part_access[part_idx]);
      for (Value_t val : part_maps[part_idx]) {
        bool done = callback(val);
        if (done) {
          return;
        }
      }
    }
  }

private:
  std::vector<Map_t> part_maps;
  std::vector<Mutex_t> part_access;
};

////////////////////////////////////////////////////////////////////////////////////////
// DBTable
//
/// DBTable stores all values as shared pointers so you don't worry about data being destroyed while in use.
template <typename Key_t, typename Value_t, typename Hasher_t = std::hash<Key_t>>
class DBTable : public PartitionedMap<Key_t, std::shared_ptr<Value_t>, Hasher_t, std::shared_mutex>
{
  using KeyType = Key_t;
  using Base_t  = PartitionedMap<Key_t, std::shared_ptr<Value_t>, Hasher_t, std::shared_mutex>;

public:
  DBTable(size_t num_partitions) : Base_t(num_partitions) {}

  std::shared_ptr<Value_t>
  obtain(Key_t const &key)
  {
    typename Base_t::ResizeLock_t lck;
    typename Base_t::Map_t &map = Base_t::lockPartMap(key, lck); // lock once
    std::shared_ptr<Value_t> &r = map[key];                      // find or alloc a shared_ptr
    if (r.get() == nullptr) {                                    // if it is new
      r.reset(new Value_t{});                                    // alloc a value, and set ptr
    }
    return r;
  }
};

/////////////////////////////////////////////
// CustomHasher
template <typename Key_t, std::size_t (*HashFn)(Key_t const &)> struct CustomHasher {
  std::size_t
  operator()(const Key_t &k) const
  {
    return HashFn(k);
  }
};

/////////////////////////////////////////////
// Helper macros
/// If you are keying on a custom class, you will need to define std::hash<Key>()
// this macro makes it easy.
#define std_hasher_macro(T, var, hash_var_expr) \
  namespace std                                 \
  {                                             \
    template <> struct hash<T> {                \
      std::size_t                               \
      operator()(const T &var) const            \
      {                                         \
        return hash_var_expr;                   \
      }                                         \
    };                                          \
  }
