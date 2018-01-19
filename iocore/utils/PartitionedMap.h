
#include <Mutex.h>

/// Intended to make datasets thread safe by assigning locks to partitions of data.
/** Allocates a fixed number of locks and retrives them with a hash. */
struct LockPool {
  using Index_t = uint8_t;

  LockPool(size_t num_locks) : m_size(num_locks), m_mutex(new Mutex[num_locks]) {}

  Index_t
  getIndex(size_t key_hash)
  {
    return key_hash % m_size;
  }

  Mutex &
  getMutex(Index_t index)
  {
    return m_mutex[index];
  }

  size_t
  size()
  {
    return m_size;
  }

private:
  const size_t m_size = 0;
  Mutex m_mutex[];

  LockPool() = 0;
};

/// Intended to provide a thread safe lookup. Uses one lock for the duration of the lookup.
template <typename Key_t, typename ValuePtr_t> struct LookupMap {
public:
  using Map_t = std::unordered_map<size_t, ValuePtr_t>;

  LookupMap() {}

  /// returns a value reference.
  ValuePtr_t
  get(const Key_t &key)
  {
    m_mutex->lock();

    ValuePtr ptr = {};
    auto elm     = m_map.find(hash);
    if (elm != map.end()) {
      ptr = *elm;
    }

    m_mutex->unlock();

    return ptr;
  }

  void
  put(const Key_t &key, ValuePtr_t &val)
  {
    m_mutex->lock();

    m_maps[key] = val;

    m_mutex->unlock();
  }

private:
  Map_t m_map;
  Mutex m_mutex; ///< this lock only protects the map, NOT the data it points to.
};

/// Intended to provide a thread safe lookup. Only the part of the map is locked, for the duration of the lookup.
template <typename Key_t, typename ValuePtr_t> struct PartitionedMap {
public:
  using Map_t = std::unordered_map<size_t, ValuePtr_t>;

  PartitionedMap(size_t num_partitions) : m_maps(new Map_t[num_partitions]), m_lock_pool(num_partitions) {}

  /// returns a value reference.
  ValuePtr_t
  get(const Key_t &key)
  {
    size_t hash                = std::hash<Key>()(key);
    auto part_idx              = m_lock_pool.getIndex(hash);
    Mutex *map_partition_mutex = m_lock_pool.getMutex(part_idx);

    map_partition_mutex->lock();

    Map_t &map   = m_maps[part_idx];
    ValuePtr ptr = {};
    auto elm     = map.find(hash);
    if (elm != map.end()) {
      ptr = *elm;
    }

    map_partition_mutex->unlock();

    return ptr;
  }

  void
  put(const Key_t &key, ValuePtr_t &val)
  {
    size_t hash                = std::hash<Key>()(key);
    auto part_idx              = m_lock_pool.getIndex(hash);
    Mutex *map_partition_mutex = m_lock_pool.getMutex(part_idx);

    map_partition_mutex->lock();

    m_maps[part_idx][hash] = val;

    map_partition_mutex->unlock();
  }

  ValuePtr_t
  visit(bool (*func)(ValuePtr_t, void *), void *ref)
  {
    for (int part_idx = 0; part_idx < m_lock_pool.size(); part_idx) {
      ScopedMutexLock part_lock(m_lock_pool.getMutex(part_idx));

      for (ValuePtr_t val : m_maps[part_idx]) {
        bool done = func(val, ref);
        if (done) {
          return val;
        }
      }
    }
  }

  ValuePtr_t
  visit(std::function<bool(ValuePtr_t &)> lambda)
  {
    for (int part_idx = 0; part_idx < m_lock_pool.size(); part_idx) {
      ScopedMutexLock lock(m_lock_pool.getMutex(part_idx));

      for (ValuePtr_t val : m_maps[part_idx]) {
        bool done = lambda(val);
        if (done) {
          return val;
        }
      }
    }
  }

private:
  using Map_t = std::unordered_map<size_t, ValuePtr_t>;

  Map_t m_maps[];
  LockPool m_lock_pool;
};

/// If you are keying on a custom class, you will need to define std::hash<Key>()
// this macro makes it easy.
#define std_hasher_macro(T, var, hash_var_expr) \
  namespace std                                 \
  {                                             \
    template <> struct hash<T> {                \
      std::size_t                               \
      operator()(const T &var) const            \
      {                                         \
        return hash_var_expr                    \
      }                                         \
    };                                          \
  };